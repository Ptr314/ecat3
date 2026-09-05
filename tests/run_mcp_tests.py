#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Прогон тех же сценариев через MCP-сервер и сравнение с теми же эталонами.

    python tests/run_mcp_tests.py             весь набор, кроме медленных
    python tests/run_mcp_tests.py --all       вместе с медленными
    python tests/run_mcp_tests.py -k bk       только тесты, в имени которых есть bk
    python tests/run_mcp_tests.py --exe ПУТЬ  конкретная сборка

Зачем это отдельно от run_tests.py. Вывод сценария идет в лог двумя путями:
в файл (его проверяет run_tests.py по всем машинам) и в сток ScriptSink,
которым отвечает MCP-сервер. Второй путь не проверялся ничем, кроме ручных
сессий, а расходится он тихо: команда выполнилась, ответ пустой.

Что делает раннер: поднимает `эмулятор --mcp`, загружает машину сценария через
ecat_machine, отдает остальные строки одним вызовом ecat_run и сличает
собранный ответ с tests/expected/<тест>.txt &ndash; тем же эталоном, что и
обычный прогон.

Сценарий отдается не совсем дословно:

* строка MACHINE выполняется отдельным вызовом ecat_machine, иначе к выводу
  добавится список устройств загруженной машины;
* SCREEN и EXIT пропускаются: снимки экрана в эталонном логе не отражаются,
  а EXIT завершил бы сервер посреди набора;
* относительные имена файлов (__../files/__, __../results/__) заменяются на
  абсолютные. В сессии MCP нет файла сценария, а значит и каталога, от которого
  такое имя считается.

Все остальное &ndash; те же строки, тот же порядок, тот же движок.
"""

import argparse
import concurrent.futures
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TESTS_DIR)

import run_tests as rt          # noqa: E402  поиск сборки, список тестов, сравнение

FILES_DIR = os.path.join(TESTS_DIR, "files")
MCP_RESULTS_DIR = os.path.join(rt.RESULTS_DIR, "mcp")

# Команды, которые в сессии MCP не воспроизводятся, см. шапку
SKIP_VERBS = ("SCREEN", "EXIT")

PROTOCOL_VERSION = "2024-11-05"


# --------------------------------------------------------------------------
# Сборка с MCP
# --------------------------------------------------------------------------

def is_headless(exe):
    return "headless" in os.path.basename(exe).lower()


def has_mcp(exe):
    """
    Собрана ли сборка с ENABLE_MCP: True, False или None, если выяснить нечем.

    Спрашивать саму программу нельзя: оконная сборка без этого ключа на
    незнакомый --mcp показывает окно с сообщением об ошибке и ждет человека.
    Поэтому смотрим в CMakeCache.txt рядом с исполняемым файлом &ndash; тот же
    файл, из которого берутся каталоги Qt и MinGW.
    """
    cache = os.path.join(os.path.dirname(exe), "CMakeCache.txt")
    try:
        with open(cache, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    m = re.search(r"^ENABLE_MCP:BOOL=(\w+)$", text, re.M)
    if m is None:
        return False            # кэш есть, а ключа в нем нет: старая сборка
    return m.group(1).upper() in ("ON", "1", "TRUE", "YES")


def pick_server(explicit):
    """
    Сборка, которая умеет --mcp. Консольная предпочтительнее оконной: она не
    открывает окон и не требует Qt рядом с собой.
    """
    candidates = rt.find_emulators(explicit)

    if explicit and has_mcp(candidates[0]) is False:
        sys.exit("%s собран без ключа ENABLE_MCP, MCP-сервера в нем нет.\n"
                 "См. MCP.md." % candidates[0])

    # Сначала консольные, дальше те, про которые известно, что MCP в них есть
    candidates.sort(key=lambda p: (0 if is_headless(p) else 1,
                                   0 if has_mcp(p) else 1))
    candidates = [p for p in candidates if has_mcp(p) is not False]
    if not candidates:
        sys.exit("Ни одна из найденных сборок не собрана с ключом ENABLE_MCP.\n"
                 "Соберите ее (см. MCP.md) или укажите ключом --exe.")

    problems = []
    for exe in candidates:
        env = rt.build_environment(exe)
        version, problem = rt.check_emulator(exe, env)
        if problem is None:
            return exe, env, version
        problems.append("  %s: %s" % (exe, problem))
    sys.exit("Ни одна из найденных сборок не запускается:\n" + "\n".join(problems) +
             "\nУкажите рабочую сборку ключом --exe или переменной ECAT3_EXE.")


# --------------------------------------------------------------------------
# Клиент MCP
# --------------------------------------------------------------------------

class McpError(Exception):
    pass


class McpServer(object):
    """
    Эмулятор, запущенный с ключом --mcp: JSON-RPC по одному объекту на строку.

    Читают stdout и stderr отдельные потоки: на Windows ждать пайп с таймаутом
    иначе нельзя, а зависший сервер должен давать понятную ошибку, а не вешать
    весь прогон.
    """

    def __init__(self, exe, env, extra_args):
        self.exe = exe
        self.proc = subprocess.Popen(
            [exe, "--mcp"] + list(extra_args),
            cwd=rt.DEPLOY_DIR, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.replies = queue.Queue()
        self.errors = []
        self.next_id = 1
        self._start(self.proc.stdout, self._read_out)
        self._start(self.proc.stderr, self._read_err)

    def _start(self, stream, target):
        t = threading.Thread(target=target, args=(stream,))
        t.daemon = True
        t.start()

    def _read_out(self, stream):
        for raw in stream:
            self.replies.put(raw.decode("utf-8", errors="replace").strip())
        self.replies.put(None)          # сервер закрыл вывод

    def _read_err(self, stream):
        for raw in stream:
            line = raw.decode("utf-8", errors="replace").rstrip()
            if line:
                self.errors.append(line)

    def _send(self, message):
        data = (json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8")
        try:
            self.proc.stdin.write(data)
            self.proc.stdin.flush()
        except (OSError, ValueError):
            raise McpError("сервер закрыл соединение" + self.stderr_note())

    def notify(self, method, params=None):
        self._send({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def call(self, method, params=None, timeout=30):
        request_id = self.next_id
        self.next_id += 1
        self._send({"jsonrpc": "2.0", "id": request_id,
                    "method": method, "params": params or {}})

        deadline = time.time() + timeout
        while True:
            left = deadline - time.time()
            if left <= 0:
                raise McpError("сервер не ответил на %s за %d с" % (method, timeout))
            try:
                line = self.replies.get(timeout=left)
            except queue.Empty:
                continue
            if line is None:
                raise McpError("сервер завершился, не ответив на %s%s"
                               % (method, self.stderr_note()))
            if not line:
                continue
            try:
                message = json.loads(line)
            except ValueError:
                raise McpError("в stdout не JSON: %s" % line[:200])
            # Ответ не на наш запрос (уведомление сервера) просто пропускается
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise McpError("%s: %s" % (method, message["error"].get("message", "")))
            return message.get("result", {})

    def tool(self, name, arguments, timeout=30):
        """Текст ответа инструмента и признак ошибки."""
        result = self.call("tools/call", {"name": name, "arguments": arguments},
                           timeout=timeout)
        parts = []
        for block in result.get("content", []):
            if block.get("type") == "text":
                parts.append(block.get("text", ""))
        return "\n".join(parts), bool(result.get("isError"))

    def handshake(self):
        self.call("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "run_mcp_tests", "version": "1"},
        })
        self.notify("notifications/initialized")

    def stderr_note(self):
        if not self.errors:
            return ""
        return " (stderr: " + "; ".join(self.errors[:5]) + ")"

    def close(self):
        try:
            self.proc.stdin.close()     # конец файла на stdin - это выход
        except (OSError, ValueError):
            pass
        try:
            self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()


# --------------------------------------------------------------------------
# Сценарий -> вызовы
# --------------------------------------------------------------------------

def as_path_prefix(path):
    return '"' + path.replace("\\", "/").rstrip("/") + "/"


def nomcp_reason(test):
    """
    Причина из строки "# @nomcp ...", если сценарий объявил себя невоспроизводимым.

    Такой сценарий смотрит на величину, зависящую от абсолютного эмулируемого
    времени, а оно у живой сессии свое: между загрузкой машины и первой командой
    машина работает столько, сколько занял вызов, и этот отрезок каждый раз
    разный. В обычном прогоне сценарий стартует вместе с машиной, поэтому там
    та же величина повторяется.
    """
    with open(test.path, encoding="utf-8") as f:
        m = re.search(r"^#\s*@nomcp\s*(.*)$", f.read(), re.M)
    if m is None:
        return None
    return m.group(1).strip() or "сценарий помечен как невоспроизводимый через MCP"


def prepare(test):
    """
    Машина сценария и его строки в том виде, в каком их принимает ecat_run.

    Выброшенная строка заменяется пустой, а не исчезает: движок считает пустые
    строки наравне с остальными, и в его сообщениях (например, о ненайденном
    устройстве) стоит номер строки. Сдвинутая нумерация разошлась бы с эталоном.
    """
    machine = None
    commands = []

    with open(test.path, encoding="utf-8") as f:
        text = f.read()

    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith("#") or s.startswith("//"):
            commands.append("")
            continue

        verb = s.split(None, 1)[0].upper()
        if verb == "MACHINE":
            if machine is None:
                quoted = s.split('"')
                machine = quoted[1] if len(quoted) > 2 else ""
            commands.append("")
            continue
        if verb in SKIP_VERBS:
            commands.append("")
            continue

        s = s.replace('"../files/', as_path_prefix(FILES_DIR))
        s = s.replace('"../results/', as_path_prefix(MCP_RESULTS_DIR))
        commands.append(s)

    return machine, commands


# --------------------------------------------------------------------------
# Прогон
# --------------------------------------------------------------------------

def run_test(test, exe, env, extra_args):
    result = {"test": test, "problems": [], "skipped": None, "seconds": 0.0}

    expected = os.path.join(rt.EXPECTED_DIR, test.name + ".txt")
    if not os.path.exists(expected):
        result["skipped"] = "нет эталонного лога, сценарий пишет только файлы"
        return result

    reason = nomcp_reason(test)
    if reason:
        result["skipped"] = reason
        return result

    machine, commands = prepare(test)
    if not machine:
        result["skipped"] = "в сценарии нет команды MACHINE"
        return result
    if not [c for c in commands if c]:
        result["skipped"] = "в сценарии нет команд, кроме MACHINE"
        return result

    actual = os.path.join(MCP_RESULTS_DIR, test.name + ".txt")
    if os.path.exists(actual):
        os.remove(actual)

    started = time.time()
    server = None
    output = None
    try:
        server = McpServer(exe, env, extra_args)
        server.handshake()

        text, failed = server.tool("ecat_machine", {"config": machine},
                                   timeout=test.timeout + 15)
        if failed:
            result["problems"].append("ecat_machine: " + text.strip())
        else:
            text, failed = server.tool(
                "ecat_run",
                {"commands": "\n".join(commands), "timeout_ms": test.timeout * 1000},
                timeout=test.timeout + 30)
            output = text
            if failed:
                result["problems"].append("ecat_run: " + text.strip())
    except McpError as e:
        result["problems"].append(str(e))
    finally:
        if server is not None:
            server.close()
            # Правило то же, что у обычного прогона: посторонний вывод в stderr
            # означает, что что-то пошло не так, даже если ответ пришел
            if server.errors:
                result["problems"].append(
                    "сообщения на stderr:\n    " + "\n    ".join(server.errors))
        result["seconds"] = time.time() - started

    if output is None:
        return result

    # "ok" сервер отвечает, когда команды ничего не напечатали
    if output.strip() == "ok":
        output = ""
    if output and not output.endswith("\n"):
        output += "\n"
    with open(actual, "w", encoding="utf-8", newline="") as f:
        f.write(output)

    result["problems"] += rt.compare_artifact(test.name + ".txt", expected, actual)
    return result


def report_one(result, index, total):
    test = result["test"]
    if result["skipped"]:
        state = "пропущен"
    elif result["problems"]:
        state = "ОШИБКА"
    else:
        state = "ок"
    print("[%2d/%d] %-34s %-8s %6.1f с" % (index, total, test.name, state,
                                           result["seconds"]))
    if result["skipped"]:
        print("       %s" % result["skipped"])


def main():
    parser = argparse.ArgumentParser(
        description="Прогон сценариев .ecat через MCP-сервер и сравнение с эталонами")
    parser.add_argument("-k", "--filter", action="append", metavar="ТЕКСТ",
                        help="запускать только тесты, в имени которых есть ТЕКСТ")
    parser.add_argument("-a", "--all", action="store_true",
                        help="вместе с медленными тестами (slow-*)")
    parser.add_argument("-j", "--jobs", type=int, default=1, metavar="N",
                        help="запускать N серверов одновременно (по умолчанию 1)")
    parser.add_argument("--exe", metavar="ПУТЬ",
                        help="эмулятор, собранный с ключом ENABLE_MCP")
    args = parser.parse_args()

    tests = rt.collect_tests(args.filter, args.all)
    if not tests:
        sys.exit("Под фильтр ничего не подошло.")

    os.makedirs(MCP_RESULTS_DIR, exist_ok=True)

    exe, env, version = pick_server(args.exe)
    try:
        where = os.path.relpath(exe, rt.REPO_DIR)
    except ValueError:
        where = exe

    extra_args = []
    if is_headless(exe):
        extra_args.append("--no-sound")
    else:
        print("Оконная сборка: при загрузке машины будет появляться окно.")

    print("Сервер:   %s%s" % (where, " (%s)" % version if version else ""))
    print("Тестов:   %d\n" % len(tests))

    results = []
    started = time.time()
    with rt.KeepIni():
        if args.jobs > 1:
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = {pool.submit(run_test, t, exe, env, extra_args): t
                           for t in tests}
                for future in concurrent.futures.as_completed(futures):
                    results.append(future.result())
                    report_one(results[-1], len(results), len(tests))
        else:
            for i, test in enumerate(tests, 1):
                results.append(run_test(test, exe, env, extra_args))
                report_one(results[-1], i, len(tests))

    results.sort(key=lambda r: r["test"].name)
    failed = [r for r in results if r["problems"]]
    skipped = [r for r in results if r["skipped"]]

    print("\n" + "=" * 72)
    if failed:
        for r in failed:
            print("\n%s — %s" % (r["test"].name, r["test"].title))
            for problem in r["problems"]:
                print("  " + problem)
        print("\n" + "=" * 72)
    print("Совпало %d из %d за %d с%s" % (
        len(results) - len(failed) - len(skipped), len(results) - len(skipped),
        round(time.time() - started),
        ", пропущено %d" % len(skipped) if skipped else ""))
    if failed:
        print("Не сошлось: " + ", ".join(r["test"].name for r in failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
