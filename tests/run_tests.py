#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Прогон сценариев .ecat по всем машинам и сравнение с эталонами.

    python tests/run_tests.py                 весь набор, кроме медленных
    python tests/run_tests.py --all           вместе с медленными
    python tests/run_tests.py -k bk           только тесты, в имени которых есть bk
    python tests/run_tests.py --update        перезаписать эталоны результатами
    python tests/run_tests.py --list          показать список тестов

Каждый сценарий запускается эмулятором с рабочим каталогом deploy/. То, что он
оставляет после себя (лог, снимки экрана, файлы), складывается в tests/results
и сравнивается с tests/expected. Подробности - в tests/README.md.
"""

import argparse
import concurrent.futures
import difflib
import glob
import os
import re
import shutil
import subprocess
import sys
import time

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(TESTS_DIR)
SCRIPTS_DIR = os.path.join(TESTS_DIR, "scripts")
EXPECTED_DIR = os.path.join(TESTS_DIR, "expected")
RESULTS_DIR = os.path.join(TESTS_DIR, "results")
DEPLOY_DIR = os.path.join(REPO_DIR, "deploy")

DEFAULT_TIMEOUT = 90            # секунд, если в сценарии нет строки "# @timeout"
TEXT_SUFFIXES = (".txt", ".asc", ".log", ".cfg", ".map", ".csv")

# Допуск на снимок экрана: доля точек, которой позволено отличаться.
#
# Точное сравнение не годится: устройства тактуются порциями, длина которых
# зависит от того, сколько времени прошло на хосте, поэтому момент снимка
# гуляет на доли миллисекунды эмулируемого времени. Видно это там, где на
# экране что-то мигает: курсор или выделенная строка меняются целиком, а это
# один-два знакоместа. Настоящая разница (изменившаяся строка, сдвинутый или
# перекрашенный экран) на порядок больше.
#
# Отдельному тесту допуск задается строкой "# @pixels <точек>" в шапке.
PIXEL_TOLERANCE_RATIO = 0.002


# --------------------------------------------------------------------------
# Поиск эмулятора
# --------------------------------------------------------------------------

# Каталоги сборки, по группам: сначала рабочие сборки из среды разработки,
# и только потом каталоги, в которых собираются выпуски — там рядом с
# исполняемым файлом может не быть библиотек, их кладет рядом уже упаковщик.
EXE_GROUPS = [
    ["src/cmake-build-*/eCat3.exe",
     "src/cmake-build-*/eCat3",
     "src/cmake-build-*/eCat3.app/Contents/MacOS/eCat3",
     "src/build*/eCat3.exe",
     "src/build*/eCat3",
     "build*/eCat3.exe",
     "build*/eCat3"],
    [".build/build/*/eCat3.exe",
     ".build/build/*/eCat3"],
    # Консольная сборка ищется последней: она дает те же результаты, но окна
    # эмулятора при прогоне не видно. На машине без Qt (то есть в CI) она
    # обычно единственная, что здесь и находится.
    ["src/cmake-build-*/eCat3-headless.exe",
     "src/cmake-build-*/eCat3-headless",
     "src/build*/eCat3-headless.exe",
     "src/build*/eCat3-headless",
     "build*/eCat3-headless.exe",
     "build*/eCat3-headless",
     ".build/build/*/eCat3-headless.exe",
     ".build/build/*/eCat3-headless"],
]


def find_emulators(explicit):
    """
    Сборки-кандидаты, от самой подходящей к остальным.

    Явно указанный путь или ECAT3_EXE берутся как есть; иначе перебираются
    каталоги сборки, внутри группы — от самой свежей. Кандидатов несколько,
    потому что в дереве обычно лежит несколько сборок, и не каждая
    запускается: сборке может не хватать библиотек рядом.
    """
    if explicit:
        if not os.path.isfile(explicit):
            sys.exit("Не найден эмулятор: %s" % explicit)
        return [os.path.abspath(explicit)]

    env = os.environ.get("ECAT3_EXE")
    if env:
        if not os.path.isfile(env):
            sys.exit("ECAT3_EXE указывает в никуда: %s" % env)
        return [os.path.abspath(env)]

    candidates = []
    for group in EXE_GROUPS:
        found = []
        for pattern in group:
            for path in glob.glob(os.path.join(REPO_DIR, pattern.replace("/", os.sep))):
                if os.path.isfile(path):
                    found.append(os.path.abspath(path))
        found.sort(key=os.path.getmtime, reverse=True)
        candidates += found

    if not candidates:
        sys.exit(
            "Не найден исполняемый файл эмулятора.\n"
            "Укажите его ключом --exe или переменной окружения ECAT3_EXE."
        )
    return candidates


def check_emulator(exe, env):
    """
    Проверка, что найденная сборка вообще запускается.

    Без этого сломанная или неукомплектованная сборка (не хватает Qt*.dll,
    не тот разряд) не дала бы ничего, кроме десятков тестов, отвалившихся по
    времени: сценарий, у которого машина не запустилась, просто стоит.
    """
    try:
        proc = subprocess.run([exe, "--version"], cwd=DEPLOY_DIR, env=env, timeout=30,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        return None, "не ответил на --version за 30 с"
    except OSError as e:
        return None, str(e)
    output = proc.stdout.decode("utf-8", errors="replace").strip()
    if proc.returncode != 0:
        # STATUS_DLL_NOT_FOUND: обычная судьба сборки, у которой рядом нет Qt
        if (proc.returncode & 0xFFFFFFFF) == 0xC0000135:
            return None, "не хватает библиотек рядом с исполняемым файлом"
        return None, "код возврата %d на --version %s" % (proc.returncode, output)
    return output, None


def pick_emulator(candidates):
    """Первая из сборок-кандидатов, которая отвечает на --version."""
    problems = []
    for exe in candidates:
        env = build_environment(exe)
        version, problem = check_emulator(exe, env)
        if problem is None:
            return exe, env, version
        problems.append("  %s: %s" % (exe, problem))
    sys.exit("Ни одна из найденных сборок не запускается:\n" + "\n".join(problems) +
             "\nУкажите рабочую сборку ключом --exe или переменной ECAT3_EXE.")


def build_environment(exe):
    """
    PATH с библиотеками Qt.

    Собранный MinGW-компилятором эмулятор не запустится без Qt*.dll и рантайма
    компилятора в PATH. Пути к ним лежат в CMakeCache.txt рядом с исполняемым
    файлом, так что искать их вручную не нужно.
    """
    env = dict(os.environ)
    if os.name != "nt":
        return env

    extra = []
    qt_bin = os.environ.get("ECAT3_QT_BIN")
    if qt_bin:
        extra.append(qt_bin)

    cache = os.path.join(os.path.dirname(exe), "CMakeCache.txt")
    if os.path.isfile(cache):
        try:
            with open(cache, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            text = ""
        # Qt6_DIR=<префикс>/lib/cmake/Qt6 -> <префикс>/bin
        m = re.search(r"^Qt[56]_DIR:PATH=(.+)$", text, re.M)
        if m:
            prefix = m.group(1)
            for _ in range(3):
                prefix = os.path.dirname(prefix)
            extra.append(os.path.join(prefix, "bin"))
        # Каталог компилятора: там же лежит рантайм MinGW
        m = re.search(r"^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$", text, re.M)
        if m:
            extra.append(os.path.dirname(m.group(1)))
        # Сторонние библиотеки (SDL2) ищутся там же, где их нашел cmake
        m = re.search(r"^CMAKE_PREFIX_PATH:[A-Z]+=(.+)$", text, re.M)
        if m:
            for prefix in m.group(1).split(";"):
                extra += [os.path.join(prefix, "bin"),
                          os.path.join(prefix, "lib", "x64"),
                          os.path.join(prefix, "lib")]

    extra = [p.replace("/", os.sep) for p in extra if p and os.path.isdir(p.replace("/", os.sep))]
    if extra:
        env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    return env


# --------------------------------------------------------------------------
# Тесты
# --------------------------------------------------------------------------

class Test(object):
    def __init__(self, path):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.tier = self.name.split("-", 1)[0]
        with open(path, encoding="utf-8") as f:
            text = f.read()
        self.title = ""
        for line in text.splitlines():
            if line.startswith("#") and not line.startswith("# @"):
                self.title = line.lstrip("# ").strip()
                break
        m = re.search(r"^#\s*@timeout\s+(\d+)", text, re.M)
        self.timeout = int(m.group(1)) if m else DEFAULT_TIMEOUT
        # Допуск на снимок экрана: сколько точек может отличаться. Нужен там,
        # где на экране есть мигающий элемент: фаза мигания задается тактами
        # процессора, а снимок делает поток отрисовки, и попасть он может
        # по обе стороны от переключения.
        m = re.search(r"^#\s*@pixels\s+(\d+)", text, re.M)
        self.pixels = int(m.group(1)) if m else None
        # Все, что сценарий пишет в tests/results
        self.artifacts = sorted(set(re.findall(r'"\.\./results/([^"]+)"', text)))


def collect_tests(filters, include_slow):
    tests = [Test(p) for p in sorted(glob.glob(os.path.join(SCRIPTS_DIR, "*.ecat")))]
    if not include_slow:
        tests = [t for t in tests if t.tier != "slow"]
    if filters:
        tests = [t for t in tests if any(f.lower() in t.name.lower() for f in filters)]
    return tests


# --------------------------------------------------------------------------
# Сравнение
# --------------------------------------------------------------------------

def normalize_text(data):
    """Текст лога без привязки к машине, на которой он снят."""
    text = data.decode("utf-8", errors="replace")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    for base in (REPO_DIR, REPO_DIR.replace("\\", "/")):
        text = text.replace(base, "<repo>")
        text = text.replace(base.lower(), "<repo>")
    text = text.replace("\\", "/")
    return text


def compare_png(expected, actual, tolerance):
    """Описание расхождения снимков; None, если картинки совпали."""
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return "снимок отличается (PIL не установлен, подробности недоступны)"

    try:
        a = Image.open(expected).convert("RGB")
        b = Image.open(actual).convert("RGB")
    except OSError as e:
        return "не читается снимок: %s" % e

    if a.size != b.size:
        return "размер экрана %dx%d вместо %dx%d" % (b.size + a.size)

    diff = ImageChops.difference(a, b)
    box = diff.getbbox()
    if box is None:
        return None
    total = a.size[0] * a.size[1]
    if tolerance is None:
        tolerance = int(total * PIXEL_TOLERANCE_RATIO)
    changed = sum(1 for px in diff.convert("L").tobytes() if px)
    if changed <= tolerance:
        return None

    out = os.path.join(RESULTS_DIR, os.path.basename(actual)[:-4] + ".diff.png")
    try:
        merged = Image.new("RGB", (a.size[0] * 3 + 20, a.size[1]), (32, 32, 32))
        merged.paste(a, (0, 0))
        merged.paste(b, (a.size[0] + 10, 0))
        merged.paste(diff, (a.size[0] * 2 + 20, 0))
        merged.save(out)
        where = ", см. %s" % os.path.relpath(out, REPO_DIR)
    except OSError:
        where = ""
    return "отличается %d точек из %d (%.2f%%, допуск %d), область %s%s" % (
        changed, total, 100.0 * changed / total, tolerance, box, where)


def compare_artifact(name, expected_path, actual_path, tolerance=None):
    """Строки с описанием расхождений одного файла."""
    if not os.path.exists(actual_path):
        return ["%s: файл не создан" % name]
    if not os.path.exists(expected_path):
        return ["%s: нет эталона (создайте его ключом --update)" % name]

    with open(expected_path, "rb") as f:
        expected = f.read()
    with open(actual_path, "rb") as f:
        actual = f.read()
    if expected == actual:
        return []

    if name.lower().endswith(".png"):
        message = compare_png(expected_path, actual_path, tolerance)
        return [] if message is None else ["%s: %s" % (name, message)]

    if name.lower().endswith(TEXT_SUFFIXES):
        e = normalize_text(expected).splitlines(True)
        a = normalize_text(actual).splitlines(True)
        if e == a:
            return []
        diff = list(difflib.unified_diff(e, a, "эталон/" + name, "результат/" + name, n=1))
        lines = ["%s: расходится с эталоном" % name]
        lines += ["    " + d.rstrip("\n") for d in diff[:40]]
        if len(diff) > 40:
            lines.append("    ... еще %d строк" % (len(diff) - 40))
        return lines

    return ["%s: содержимое отличается (%d байт вместо %d)" % (name, len(actual), len(expected))]


# --------------------------------------------------------------------------
# Прогон
# --------------------------------------------------------------------------

def script_logs(test):
    """
    Логи именно этого сценария.

    Имя лога - это имя сценария плюс время создания, поэтому по маске
    "имя-*.log" в чужой лог попасть легко: у boot-agat-9 так забирался лог
    boot-agat-9-pp. Отсюда точная проверка хвоста.
    """
    stamped = re.compile(re.escape(test.name) + r"-\d{4}(-\d{2}){5}\.log$")
    return [p for p in glob.glob(os.path.join(SCRIPTS_DIR, test.name + "-*.log"))
            if stamped.search(os.path.basename(p))]


def run_test(test, exe, env, update, exe_args=()):
    result = {"test": test, "problems": [], "artifacts": [], "seconds": 0.0}

    # Хвосты прошлого прогона, чтобы не сравнить старый файл с новым эталоном
    for path in script_logs(test):
        os.remove(path)
    for name in test.artifacts + [test.name + ".txt", test.name + ".diff.png"]:
        path = os.path.join(RESULTS_DIR, name)
        if os.path.exists(path):
            os.remove(path)

    started = time.time()
    try:
        proc = subprocess.run(
            [exe, "--script", test.path] + list(exe_args),
            cwd=DEPLOY_DIR, env=env, timeout=test.timeout,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        code = proc.returncode
        stderr = proc.stderr.decode("utf-8", errors="replace").strip()
    except subprocess.TimeoutExpired:
        result["seconds"] = time.time() - started
        result["problems"].append(
            "эмулятор не завершился за %d с (сценарий встал или машина не загрузилась)"
            % test.timeout)
        return result
    result["seconds"] = time.time() - started

    if code != 0:
        result["problems"].append("эмулятор вышел с кодом %d" % code)
    if stderr:
        result["problems"].append("сообщения на stderr:\n    " + stderr.replace("\n", "\n    "))

    # Лог сценария создается рядом со сценарием и содержит в имени время
    logs = sorted(script_logs(test), key=os.path.getmtime)
    names = []
    if logs:
        shutil.move(logs[-1], os.path.join(RESULTS_DIR, test.name + ".txt"))
        for extra in logs[:-1]:
            os.remove(extra)
        names.append(test.name + ".txt")
    names += test.artifacts
    result["artifacts"] = names

    for name in names:
        actual = os.path.join(RESULTS_DIR, name)
        expected = os.path.join(EXPECTED_DIR, name)
        if update:
            if os.path.exists(actual):
                shutil.copyfile(actual, expected)
            else:
                result["problems"].append("%s: файл не создан" % name)
            continue
        result["problems"] += compare_artifact(name, expected, actual, test.pixels)

    if not names:
        result["problems"].append("сценарий ничего не оставил после себя")
    return result


def main():
    parser = argparse.ArgumentParser(
        description="Прогон сценариев .ecat по всем машинам и сравнение с эталонами",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-k", "--filter", action="append", metavar="ТЕКСТ",
                        help="запускать только тесты, в имени которых есть ТЕКСТ")
    parser.add_argument("-a", "--all", action="store_true",
                        help="вместе с медленными тестами (slow-*)")
    parser.add_argument("-u", "--update", action="store_true",
                        help="записать полученное как новые эталоны")
    parser.add_argument("-j", "--jobs", type=int, default=1, metavar="N",
                        help="запускать N эмуляторов одновременно (по умолчанию 1)")
    parser.add_argument("-l", "--list", action="store_true",
                        help="показать список тестов и выйти")
    parser.add_argument("--exe", metavar="ПУТЬ",
                        help="исполняемый файл эмулятора (иначе берется самая свежая сборка)")
    args = parser.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8")
        except (ValueError, OSError):
            pass

    tests = collect_tests(args.filter, args.all)
    if not tests:
        sys.exit("Под условия отбора не подошел ни один тест")

    if args.list:
        for t in tests:
            print("%-32s %s" % (t.name, t.title))
        print("\nвсего: %d" % len(tests))
        return 0

    for d in (RESULTS_DIR, EXPECTED_DIR):
        os.makedirs(d, exist_ok=True)

    exe, env, version = pick_emulator(find_emulators(args.exe))
    try:
        where = os.path.relpath(exe, REPO_DIR)
    except ValueError:          # сборка на другом диске
        where = exe
    # Консольная сборка запускается без звука: на машине без звуковой карты
    # аудиодрайвер пишет в stderr, а любой вывод в stderr здесь означает
    # провалившийся тест. На вывод сценария это не влияет, звук в него не входит.
    exe_args = []
    if "headless" in os.path.basename(exe).lower():
        exe_args.append("--no-sound")

    print("Эмулятор: %s%s%s" % (where, " (%s)" % version if version else "",
                                ", без звука" if exe_args else ""))
    print("Тестов:   %d%s\n" % (len(tests), ", режим обновления эталонов" if args.update else ""))

    results = []
    started = time.time()
    with KeepIni():
        if args.jobs > 1:
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = {pool.submit(run_test, t, exe, env, args.update, exe_args): t
                           for t in tests}
                for future in concurrent.futures.as_completed(futures):
                    results.append(future.result())
                    report_one(results[-1], len(results), len(tests))
        else:
            for i, test in enumerate(tests, 1):
                results.append(run_test(test, exe, env, args.update, exe_args))
                report_one(results[-1], i, len(tests))

    results.sort(key=lambda r: r["test"].name)
    failed = [r for r in results if r["problems"]]

    print("\n" + "=" * 72)
    if failed:
        for r in failed:
            print("\n%s — %s" % (r["test"].name, r["test"].title))
            for problem in r["problems"]:
                print("  " + problem)
        print("\n" + "=" * 72)
    print("Пройдено %d из %d за %d с" % (
        len(results) - len(failed), len(results), round(time.time() - started)))
    if failed:
        print("Не сошлось: " + ", ".join(r["test"].name for r in failed))
        print("Если изменение поведения ожидаемое, обновите эталоны:")
        print("  python tests/run_tests.py --update -k " + failed[0]["test"].name)

    warn_about_orphans()
    write_report(results, failed)
    return 1 if failed else 0


def warn_about_orphans():
    """Эталоны, которых уже никто не спрашивает: сценарий переименовали или убрали."""
    known = set()
    for test in collect_tests(None, True):
        known.add(test.name + ".txt")
        known.update(test.artifacts)
    orphans = sorted(n for n in os.listdir(EXPECTED_DIR)
                     if os.path.isfile(os.path.join(EXPECTED_DIR, n)) and n not in known)
    if orphans:
        print("\nЭталоны без сценария (можно удалить): " + ", ".join(orphans))


class KeepIni(object):
    """
    Сохраняет и возвращает на место deploy/ecat.ini.

    Эмулятор переписывает свой ini при каждом выходе (запоминает последнюю
    машину, громкость, каталог), а прогон запускает его полсотни раз. Рабочий
    файл разработчика после этого должен остаться таким, каким был.
    """

    def __init__(self):
        self.path = os.path.join(DEPLOY_DIR, "ecat.ini")
        self.saved = None

    def __enter__(self):
        try:
            with open(self.path, "rb") as f:
                self.saved = f.read()
        except OSError:
            self.saved = None
        return self

    def __exit__(self, *exc):
        if self.saved is None:
            return False
        try:
            with open(self.path, "rb") as f:
                if f.read() == self.saved:
                    return False
            with open(self.path, "wb") as f:
                f.write(self.saved)
        except OSError:
            pass
        return False


def report_one(result, index, total):
    mark = "ОШИБКА" if result["problems"] else "ок"
    print("[%2d/%d] %-34s %-7s %5.1f с" % (
        index, total, result["test"].name, mark, result["seconds"]))
    sys.stdout.flush()


def write_report(results, failed):
    path = os.path.join(RESULTS_DIR, "report.txt")
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write("Прогон %s\n\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
            for r in results:
                f.write("%-34s %-7s %5.1f с\n" % (
                    r["test"].name, "ОШИБКА" if r["problems"] else "ок", r["seconds"]))
            if failed:
                f.write("\n")
                for r in failed:
                    f.write("\n%s\n" % r["test"].name)
                    for problem in r["problems"]:
                        f.write("  %s\n" % problem)
    except OSError:
        pass


if __name__ == "__main__":
    sys.exit(main())
