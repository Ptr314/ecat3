# Прогон тестов Alex_K (UTEST.DSK) на эмуляторе через MCP-сервер консольной
# сборки: по процессу на тест, параллельно, на SPEED MAX. Машина грузит RT-11
# с дискеты, набирает RU <тест>, листает страницы («Press <Enter>») и снимает
# экраны; текст с них читается по шрифту utest_font.json (знакоместо 8 x 11,
# фаза строк ищется, потому что RT-11 прокручивает экран не на целую строку).
#
#   py run_utest.py OUT [-j 8] [--cfg uknc/UKNC.cfg] [--disk UTEST.DSK] TEST...
#   py alexk.py OUT                          - сравнение с машинами
#
# Диск по умолчанию - docs-external/UKNC/Alex_K/UTEST.DSK; каждый тест
# получает свою копию: эмулятор пишет дискету.
import argparse, concurrent.futures, json, os, shutil, sys, time
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tests'))
import run_tests as rt               # noqa: E402
import run_mcp_tests as rmt          # noqa: E402

FONT = os.path.join(HERE, 'utest_font.json')
PY, PX = 11, 8
_font = json.load(open(FONT))

def _cells(px, w, h, y0):
    rows = []
    for r in range((h - y0) // PY):
        rows.append([''.join('1' if px[c * PX + x, y0 + r * PY + y] == (255, 255, 255) else '0'
                             for y in range(PY) for x in range(PX)) for c in range(w // PX)])
    return rows

def screen_text(path):
    im = Image.open(path).convert('RGB')
    px = im.load()
    best, rows = None, None
    for y0 in range(PY):
        c = _cells(px, im.size[0], im.size[1], y0)
        s = sum(1 if b in _font else -1 for row in c for b in row if '1' in b)
        if best is None or s > best:
            best, rows = s, c
    return [''.join(' ' if '1' not in b else _font.get(b, '?') for b in row).rstrip() for row in rows]

def run_one(test, out, exe, env, cfg, disk_src, step, limit):
    work = os.path.join(out, test)
    os.makedirs(work, exist_ok=True)
    disk = os.path.join(work, 'disk.dsk')
    shutil.copy(disk_src, disk)
    fw = lambda p: p.replace('\\', '/')
    srv = rmt.McpServer(exe, env, ['--no-sound'])
    pages = []
    lines = []
    try:
        srv.handshake()
        t, f = srv.tool('ecat_machine', {'config': cfg}, timeout=60)
        if f: return test, 'machine: ' + t
        # Меню ЗАГРУЗКА: 1 - диск, привод 0
        cmds = ['SPEED MAX', 'COMMAND fdd0.load("%s")' % fw(disk), 'WAIT 12000',
                'KEY 100,100,1', 'WAIT 200', 'KEY 100,100,ret', 'WAIT 22000',
                'TYPE "RU %s\\n"' % test]
        t, f = srv.tool('ecat_run', {'commands': '\n'.join(cmds), 'timeout_ms': 300000}, timeout=320)
        if f: return test, 'boot: ' + t
        n, elapsed, last, same = 0, 0, None, 0
        while elapsed < limit:
            png = os.path.join(work, 's%03d.png' % n)
            t, f = srv.tool('ecat_run', {'commands': 'WAIT %d\nSCREEN "%s"' % (step, fw(png)),
                                         'timeout_ms': 600000}, timeout=620)
            # Под нагрузкой SCREEN ждёт поток отрисовки дольше, чем сервер
            # ждёт хода часов, и тот отвечает «stalled»: снимок повторяется
            tries = 0
            while f and '[stalled]' in t and tries < 20:
                time.sleep(0.5)
                tries += 1
                if os.path.exists(png):
                    f = False
                    break
                srv.tool('ecat_cancel', {}, timeout=60)
                t, f = srv.tool('ecat_run', {'commands': 'SCREEN "%s"' % fw(png), 'timeout_ms': 60000},
                                timeout=80)
            if f: return test, 'run: ' + t
            elapsed += step
            lines = screen_text(png)
            body = [l for l in lines if l.strip()]
            tail = body[-1].strip() if body else ''
            if 'Press' in tail:
                pages.append(lines)
                srv.tool('ecat_run', {'commands': 'KEY 100,100,ret'}, timeout=60)
                n, same, last = n + 1, 0, None
                continue
            if tail == '.' and any(('RU ' + test) in l for l in lines):
                pages.append(lines)
                break
            same = same + 1 if lines == last else 0
            if same >= 4:                       # тест ждёт чего-то или завис
                pages += [lines, ['*** STALL ***']]
                break
            last = lines
            n += 1
        else:
            pages += [lines, ['*** LIMIT ***']]
    finally:
        srv.close()
        if srv.errors:
            pages.append(['STDERR: ' + e for e in srv.errors])
    with open(os.path.join(out, test + '.txt'), 'w', encoding='utf-8') as fo:
        for p in pages:
            fo.write('\n'.join(p) + '\n----\n')
    return test, 'ok, %d screens' % len(pages)

# Сервер считает часы остановившимися, если они не сдвинулись за 250 мс
# настоящего времени, и на загруженной машине такое бывает с любой командой:
# тест тогда начинается заново
def run_retry(*args):
    for attempt in range(3):
        try:
            test, status = run_one(*args)
        except Exception as e:          # снимок не записался и т.п.
            test, status = args[0], 'error: %s' % e
        if status.startswith('ok'):
            break
    return test, status

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('tests', nargs='+')
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--exe', default=os.path.join(REPO, 'src', 'cmake-build-mcp', 'eCat3-headless.exe'))
    ap.add_argument('--cfg', default='uknc/UKNC.cfg')
    ap.add_argument('--disk', default=os.path.join(REPO, 'docs-external', 'UKNC', 'Alex_K', 'UTEST.DSK'))
    ap.add_argument('--step', type=int, default=10000, help='мс эмулированного времени между снимками')
    ap.add_argument('--limit', type=int, default=1500000)
    a = ap.parse_args()
    env = rt.build_environment(a.exe)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    t0 = time.time()
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        futs = [ex.submit(run_retry, t, out, a.exe, env, a.cfg, os.path.abspath(a.disk), a.step, a.limit)
                for t in a.tests]
        for fu in concurrent.futures.as_completed(futs):
            print(*fu.result(), '%.0f s' % (time.time() - t0), flush=True)

if __name__ == '__main__':
    main()
