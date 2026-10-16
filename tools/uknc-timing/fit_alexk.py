# Подбор памяти УК-НЦ по таблицам Alex_K (alexk.py) на копии расчёта
# эмулятора (emu_model.py): случайный поиск, затем спуск от лучших точек.
#
#   py fit_alexk.py cpu [avg|001|031] [N]   - cpu-mem по таблицам ЦП
#   py fit_alexk.py ppu [001|031|avg] [N]   - ppu-mem по таблицам ПП
#
# ЦП: его кварц с сеткой не связан, фаза на машине плывёт, и цель - среднее
# двух машин. Ошибка считается по группам с весами (все двухадресные формы,
# частые из них - режимы 0-2, OP1, JMP/JSR, ветвления и NOP): иначе 1728 форм
# двухадресных таблиц перевешивают регистровые команды, которых в программах
# больше всего. Группа reg - регистры ЦП, которые отвечают сразу (CRDWR1:
# MOV (R0),R1 и MOV R1,(R0) - 25.6 такта, BIS R1,(R0) и MOVB R1,(R0) - 30.8 на
# обеих машинах): код идёт из ОЗУ, а цикл данных памяти не ждёт. Без неё
# чтение-запись регистра выходила 32.0: период 32 такта кратен сетке, и
# выборка кода всё время попадала на долгий ответ (кристалл на tb_uknc.v с
# регистром по fast - 30.7).
#
#   FIT_START='{"grid": 160, ...}' - спуск только от этой точки
#
# ПП синхронен видеоконтроллеру: обе машины дают времена, кратные 4 тактам,
# а с К1515ХМ2-001 ПП читает на 4 такта быстрее, чем с К1515ХМ1-031. Модель в одной фазе ведёт себя
# как одна из них, «средней» у неё нет (усреднение по фазам даёт его только на
# бумаге: эмулятор стоит в одной фазе). Поэтому ПП подбирается под машину, и
# ошибка берётся худшая по четырём фазам сетки - чтобы точка не зависела от
# того, где окажется ПП эмулятора.
import json, os, multiprocessing as mp, random, sys
import emu_model as em
import alexk

SIDE = sys.argv[1] if len(sys.argv) > 1 else 'cpu'
TARGET = sys.argv[2] if len(sys.argv) > 2 else ('avg' if SIDE == 'cpu' else '001')
N = int(sys.argv[3]) if len(sys.argv) > 3 else 400
PRE, PERIOD = ('C', 125000) if SIDE == 'cpu' else ('P', 160000)
WEIGHTS = ({'op2': 0.2, 'key': 0.3, 'op1': 0.15, 'jmp': 0.1, 'br': 0.25, 'reg': 0.1, 'trap': 0.02} if SIDE == 'cpu'
           else {'op2': 1})

M1, M3 = alexk.machine('001'), alexk.machine('031')
DATA = []
for k, (g, w, taken) in alexk.forms(PRE).items():
    a, b = M1.get(k), M3.get(k)
    if SIDE == 'ppu' and g == 'key': g = 'op2'
    if a is None or b is None or g not in WEIGHTS: continue
    t = {'001': a, '031': b, 'avg': (a + b) / 2}[TARGET]
    DATA.append((g, w, taken, t))
if SIDE == 'cpu':
    for w, t in ((0o011001, 25.6), (0o010110, 25.6), (0o050110, 30.8), (0o110110, 30.8)):
        DATA.append(('reg', w, True, t))
    DATA.append(('br', 0o077001, True, 30.8))      # CSOB: шаг SOB R0,. на обеих машинах
    # CBR: ловушка с RTI обработчика и RTI отдельно, среднее машин (001, 031)
    DATA.append(('trap', (0o000003, 0o000002), True, (200.4 + 198.2) / 2))
    DATA.append(('trap', (0o000002,), True, (73.5 + 70.4) / 2))

class Register:
    """регистр, который отвечает сразу"""
    def answer(self, *a): return 0

def error(p, phase=None):
    extra = dict(drift=100) if SIDE == 'cpu' else {}
    if phase is not None: extra['phase'] = phase
    cache, err = {}, {g: [] for g in WEIGHTS}
    for g, w, taken, t in DATA:
        key = (w, taken, g == 'reg')
        if key not in cache:
            try: cache[key] = em.period(w, em.Mem(PERIOD, **extra, **p), taken=taken,
                                        dmem=Register() if g == 'reg' else None)
            except ValueError: cache[key] = None
        v = cache[key]
        if v is not None: err[g].append(abs(v - t))
    return sum(WEIGHTS[g] * sum(e) / len(e) for g, e in err.items() if e)

def obj(p):
    if SIDE == 'cpu': return error(p)
    return max(error(p, ph) for ph in (0, 160, 320, 480))

def sample(rng):
    if SIDE == 'cpu':
        return dict(grid=rng.choice([160, 320, 640]), setup=rng.choice(range(0, 400, 40)),
                    reply=rng.choice(range(80, 800, 40)), reply_write=rng.choice(range(80, 800, 40)),
                    read=rng.choice(range(320, 2400, 80)), write=rng.choice(range(320, 3200, 80)),
                    rmw_write=rng.choice(range(160, 2000, 80)), late=rng.choice([0, 0, 1]))
    return dict(grid=rng.choice([320, 640]), setup=rng.choice(range(0, 640, 80)),
                reply=rng.choice(range(80, 960, 80)), reply_write=rng.choice(range(80, 960, 80)),
                read=rng.choice(range(1280, 4480, 160)), write=rng.choice(range(1280, 4480, 160)),
                write_byte=rng.choice(range(640, 3840, 160)), late=rng.choice([0, 0, 1]))

def neighbours(p):
    out = []
    for k, v in p.items():
        if k == 'grid':
            out += [dict(p, grid=g) for g in (160, 320, 640) if g != v]
        elif k == 'late':
            out.append(dict(p, late=1 - v))
        else:
            out += [dict(p, **{k: v + d}) for d in (-160, -80, -40, 40, 80, 160) if v + d >= 0]
    return out

if __name__ == '__main__':
    rng = random.Random(2)
    start = os.environ.get('FIT_START')
    cands = [json.loads(start)] if start else [sample(rng) for _ in range(N)]
    N = len(cands)
    with mp.Pool() as pool:
        res = pool.map(obj, cands, chunksize=2)
        order = sorted(range(N), key=lambda i: res[i])
        for i in order[:4]:
            cur, best = cands[i], res[i]
            while True:
                nb = neighbours(cur)
                r = pool.map(obj, nb)
                j = min(range(len(nb)), key=lambda j: r[j])
                if r[j] >= best - 1e-6: break
                cur, best = nb[j], r[j]
            print('%.3f' % best, json.dumps(cur), flush=True)
