# -*- coding: utf-8 -*-
"""Обёртка MCP-сервера eCat3, переживающая пересборку и падение эмулятора.

Запущенный `eCat3 --mcp` живёт всю сессию клиента, и это давало две беды:
исполняемый файл держался открытым, так что линковщик не мог его перезаписать
(`cannot open output file eCat3.exe: Permission denied`), а отвечал сервер тем
кодом, с которым стартовал, — проверить свежую правку ядра было нельзя.

Скрипт встаёт между клиентом и эмулятором и решает обе:

* эмулятор запускается не из каталога сборки, а из временной копии, поэтому
  пересобирать можно когда угодно;
* перед каждым вызовом инструмента сверяется время сборки. Стал файл новее
  копии — эмулятор перезапускается из нового, ему повторяется рукопожатие
  `initialize`, и запрос уходит уже новому ядру. Клиент ничего не замечает,
  только к ответу приписывается строка о перезапуске.

Машина при перезапуске, разумеется, теряется: это новый процесс. Поэтому
после перезапуска её надо загрузить заново (`ecat_machine`), о чём и говорит
приписанная к ответу строка.

Падение эмулятора обёртку не убивает. Прежде она выходила вместе с ним, и
сервер пропадал до конца сессии клиента — сам клиент его не поднимает. Теперь
на повисший запрос уходит ответ с ошибкой, а следующий вызов запускает
эмулятор заново. Копия сборки снимается до остановки старого процесса и с
ожиданием, пока линковщик допишет файл: вызов посреди сборки больше не
оставляет сервер без эмулятора.

Всё, что происходит с процессом, — запуски, перезапуски, коды выхода —
пишется в журнал `%TEMP%/ecat3-mcp-reload.log`: stderr обёртки клиент
прячет, и без журнала причину падения не найти.

Обмен по стандарту MCP — построчный JSON-RPC, по строке на сообщение, так что
разбирать нужно только метод запроса и идентификатор ответа; всё остальное
пересылается байт в байт.

Запуск (так это прописано в `.mcp.json` в корне репозитория):

    py .build/mcp_reload.py --exe <путь к eCat3.exe> -- --mcp --workdir <deploy>

Переменная окружения `ECAT3_MCP_NO_RELOAD=1` выключает перезапуск по новой
сборке: обёртка тогда только снимает блокировку файла и поднимает упавший
эмулятор.
"""

import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

# Идентификатор, под которым обёртка повторяет initialize новому эмулятору.
# Клиентские идентификаторы такого вида не встречаются, так что ответ на него
# ни с чем не спутать и наружу его отдавать не надо.
HANDSHAKE_ID = "ecat3-mcp-reload-handshake"

PREFIX = "ecat3-mcp-"
JOURNAL = os.path.join(tempfile.gettempdir(), "ecat3-mcp-reload.log")

NOTE = ("Эмулятор перезапущен из новой сборки: это другой процесс, и всё, что было "
        "настроено в прошлом, потеряно — машину надо загрузить заново (ecat_machine).")

NOTE_CRASH = ("Эмулятор перед этим завершился сам (%s) и запущен заново: машину надо "
              "загрузить заново (ecat_machine). Подробности — в " + JOURNAL)

# Сколько ждать, пока линковщик допишет исполняемый файл, секунд
COPY_WAIT = 60.0


def log(message):
    sys.stderr.write("[mcp_reload] %s\n" % message)
    sys.stderr.flush()
    try:
        with io.open(JOURNAL, "a", encoding="utf-8") as f:
            f.write(u"%s [%d] %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), os.getpid(), message))
    except Exception:
        pass


def describe_exit(code):
    """Код выхода словами. Падение под Windows - это код вида 0xC0000005."""
    if code is None:
        return "код выхода неизвестен"
    code &= 0xFFFFFFFF
    if code >= 0x80000000:
        return "код 0x%08X" % code
    return "код %d" % code


def sweep_old_copies():
    """Убирает копии, оставшиеся от прошлых запусков.

    Обёртку обычно не закрывают, а убивают вместе с клиентом, и убрать за
    собой она тогда не успевает. Поэтому чужие каталоги подметаются на старте:
    у работающей сейчас обёртки её копия занята операционной системой, удалить
    такую не выйдет, и живой сеанс переживёт уборку.
    """
    root = tempfile.gettempdir()
    try:
        names = os.listdir(root)
    except OSError:
        return
    for name in names:
        if name.startswith(PREFIX):
            shutil.rmtree(os.path.join(root, name), ignore_errors=True)


class Wrapper(object):
    def __init__(self, exe, args, reload_enabled):
        self.exe = os.path.abspath(exe)
        self.args = args
        self.reload_enabled = reload_enabled
        sweep_old_copies()
        self.dir = tempfile.mkdtemp(prefix=PREFIX)
        self.copies = 0
        self.built_at = None            # время сборки, с которого снята копия
        self.child = None
        self.pump = None
        # Растёт при каждой замене процесса: поток пересылки по нему узнаёт, что
        # его эмулятор закончился не сам, а был заменён
        self.generation = 0
        # Эмулятор завершился сам: описание кода выхода, иначе None
        self.dead = None
        # Рукопожатие клиента, чтобы повторить его новому процессу
        self.initialize = None
        self.initialized = None
        # Идентификатор запроса, к ответу на который надо приписать строку
        self.note_for = None
        self.note_text = None
        # Запросы, отправленные эмулятору и ещё не отвеченные
        self.pending = set()
        self.lock = threading.Lock()
        self.out_lock = threading.Lock()

    # ------------------------------------------------------------ процесс

    def make_copy(self):
        """Снимает копию сборки, дождавшись, пока линковщик её допишет."""
        deadline = time.time() + COPY_WAIT
        last_error = None
        while True:
            try:
                built_at = os.path.getmtime(self.exe)
                self.copies += 1
                target = os.path.join(self.dir, str(self.copies))
                os.makedirs(target)
                target = os.path.join(target, os.path.basename(self.exe))
                shutil.copyfile(self.exe, target)
                # Файл не менялся, пока копировался, - значит, дописан
                if os.path.getmtime(self.exe) == built_at:
                    return target, built_at
                last_error = "файл менялся во время копирования"
            except (OSError, IOError) as error:
                last_error = str(error)
            if time.time() > deadline:
                raise RuntimeError("не удалось снять копию сборки: %s" % last_error)
            time.sleep(1.0)

    def launch(self, copy, built_at):
        child = subprocess.Popen([copy] + self.args,
                                 stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE)
        self.built_at, self.child = built_at, child
        with self.lock:
            self.dead = None
        log("эмулятор запущен, pid %d, %s" % (child.pid, copy))

    def stop(self):
        if self.child is None:
            return
        child = self.child
        self.child = None
        try:
            child.stdin.close()
        except Exception:
            pass
        try:
            child.wait(timeout=5)
        except Exception:
            child.kill()
            try:
                child.wait(timeout=5)
            except Exception:
                pass

    def rebuilt(self):
        if not self.reload_enabled:
            return False
        try:
            return os.path.getmtime(self.exe) != self.built_at
        except OSError:
            return False

    # ------------------------------------------------------------ потоки

    def write_out(self, data):
        with self.out_lock:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()

    def to_child(self, data):
        self.child.stdin.write(data)
        self.child.stdin.flush()

    def answer_error(self, request_id, text):
        message = {"jsonrpc": "2.0", "id": request_id,
                   "error": {"code": -32603, "message": text}}
        self.write_out((json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8"))

    def start_pump(self):
        """Пересылает ответы эмулятора клиенту, дописывая строку после перезапуска."""
        child = self.child
        generation = self.generation

        def run():
            for line in child.stdout:
                try:
                    reply_id = json.loads(line.decode("utf-8")).get("id")
                    with self.lock:
                        self.pending.discard(reply_id)
                except Exception:
                    pass
                self.write_out(self.annotate(line))
            try:
                code = child.wait(timeout=5)
            except Exception:
                code = None
            if generation != self.generation:
                return
            # Эмулятор закончился сам. Обёртка остаётся: иначе сервер пропал бы
            # до конца сессии. Повисшим запросам - ошибка, дальше - новый запуск
            what = describe_exit(code)
            log("эмулятор завершился сам, %s" % what)
            with self.lock:
                self.dead = what
                pending = list(self.pending)
                self.pending.clear()
            for request_id in pending:
                self.answer_error(request_id,
                    "Эмулятор завершился (%s). Следующий вызов запустит его заново; "
                    "подробности — в %s" % (what, JOURNAL))

        self.pump = threading.Thread(target=run)
        self.pump.daemon = True
        self.pump.start()

    def annotate(self, line):
        if self.note_for is None:
            return line
        try:
            message = json.loads(line.decode("utf-8"))
            if message.get("id") != self.note_for:
                return line
            self.note_for = None
            content = message.get("result", {}).get("content")
            if not isinstance(content, list):
                return line
            content.append({"type": "text", "text": self.note_text})
            return (json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8")
        except Exception:
            return line

    # ------------------------------------------------------------ перезапуск

    def restart(self, reason):
        """Поднимает новый эмулятор и повторяет ему рукопожатие клиента.

        Копия снимается до остановки старого процесса: не вышло её снять -
        старый эмулятор продолжает работать, а не остаётся пустое место."""
        log("перезапуск: %s" % reason)
        copy, built_at = self.make_copy()
        self.generation += 1
        self.stop()
        self.launch(copy, built_at)

        # Рукопожатие проходит синхронно, до запуска пересылки: ответ на него
        # предназначен обёртке, а не клиенту, который уже проинициализирован
        if self.initialize is not None:
            request = json.loads(self.initialize.decode("utf-8"))
            request["id"] = HANDSHAKE_ID
            self.to_child((json.dumps(request) + "\n").encode("utf-8"))
            while True:
                line = self.child.stdout.readline()
                if not line:
                    raise RuntimeError("новый эмулятор не ответил на initialize")
                try:
                    if json.loads(line.decode("utf-8")).get("id") == HANDSHAKE_ID:
                        break
                except ValueError:
                    pass
            if self.initialized is not None:
                self.to_child(self.initialized)

        self.start_pump()

    # ------------------------------------------------------------ главный цикл

    def run(self):
        log("обёртка запущена: %s" % self.exe)
        copy, built_at = self.make_copy()
        self.launch(copy, built_at)
        self.start_pump()

        for line in sys.stdin.buffer:
            method, request_id = None, None
            try:
                message = json.loads(line.decode("utf-8"))
                method = message.get("method")
                request_id = message.get("id")
            except ValueError:
                pass

            if method == "initialize":
                self.initialize = line
            elif method == "notifications/initialized":
                self.initialized = line
            elif method == "tools/call":
                with self.lock:
                    dead = self.dead
                reason, note = None, None
                if dead is not None:
                    reason, note = "эмулятор завершился сам (%s)" % dead, NOTE_CRASH % dead
                elif self.rebuilt():
                    reason, note = "сборка обновилась", NOTE
                if reason is not None:
                    try:
                        self.restart(reason)
                        self.note_for, self.note_text = request_id, note
                    except Exception as error:
                        log("перезапуск не удался: %s" % error)
                        if request_id is not None:
                            self.answer_error(request_id,
                                "Не удалось запустить эмулятор: %s. Подробности — в %s"
                                % (error, JOURNAL))
                        continue

            if request_id is not None and method is not None:
                with self.lock:
                    self.pending.add(request_id)
            try:
                self.to_child(line)
            except Exception as error:
                # Процесс уже умер, а поток пересылки ещё не заметил: ответить
                # сразу, следующий вызов его поднимет
                log("не удалось передать запрос эмулятору: %s" % error)
                with self.lock:
                    self.pending.discard(request_id)
                    if self.dead is None:
                        self.dead = "канал к процессу закрыт"
                if request_id is not None:
                    self.answer_error(request_id,
                        "Эмулятор не отвечает (%s). Следующий вызов запустит его заново"
                        % error)

        # Тем же счётчиком глушится и поток пересылки: иначе он увидит конец
        # вывода эмулятора, решит, что тот умер сам, и начнёт отвечать ошибками
        log("клиент закрыл канал, обёртка завершается")
        self.generation += 1
        self.stop()
        shutil.rmtree(self.dir, ignore_errors=True)


def main():
    argv = sys.argv[1:]
    if len(argv) < 2 or argv[0] != "--exe":
        sys.stderr.write(__doc__)
        return 2
    exe = argv[1]
    args = argv[2:]
    if args and args[0] == "--":
        args = args[1:]

    if not os.path.isfile(exe):
        log("нет исполняемого файла: %s" % exe)
        return 1

    reload_enabled = os.environ.get("ECAT3_MCP_NO_RELOAD", "") not in ("1", "yes", "true")
    Wrapper(exe, args, reload_enabled).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
