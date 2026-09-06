# -*- coding: utf-8 -*-
"""Обёртка MCP-сервера eCat3, переживающая пересборку эмулятора.

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

Обмен по стандарту MCP — построчный JSON-RPC, по строке на сообщение, так что
разбирать нужно только метод запроса и идентификатор ответа; всё остальное
пересылается байт в байт.

Запуск (так это прописано в `.mcp.json` в корне репозитория):

    py .build/mcp_reload.py --exe <путь к eCat3.exe> -- --mcp --workdir <deploy>

Переменная окружения `ECAT3_MCP_NO_RELOAD=1` выключает перезапуск: обёртка
тогда только снимает блокировку файла.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading

# Идентификатор, под которым обёртка повторяет initialize новому эмулятору.
# Клиентские идентификаторы такого вида не встречаются, так что ответ на него
# ни с чем не спутать и наружу его отдавать не надо.
HANDSHAKE_ID = "ecat3-mcp-reload-handshake"

NOTE = ("Эмулятор перезапущен из новой сборки: это другой процесс, и всё, что было "
        "настроено в прошлом, потеряно — машину надо загрузить заново (ecat_machine).")


PREFIX = "ecat3-mcp-"


def log(message):
    sys.stderr.write("[mcp_reload] %s\n" % message)
    sys.stderr.flush()


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
        self.copy = os.path.join(self.dir, os.path.basename(self.exe))
        self.built_at = None            # время сборки, с которого снята копия
        self.child = None
        self.pump = None
        # Растёт при каждом перезапуске: поток пересылки по нему узнаёт, что
        # его эмулятор закончился не сам, а был заменён, и выходить не надо
        self.generation = 0
        # Рукопожатие клиента, чтобы повторить его новому процессу
        self.initialize = None
        self.initialized = None
        # Идентификатор запроса, к ответу на который надо приписать NOTE
        self.note_for = None
        self.out_lock = threading.Lock()

    # ------------------------------------------------------------ процесс

    def spawn(self):
        self.built_at = os.path.getmtime(self.exe)
        shutil.copyfile(self.exe, self.copy)
        self.child = subprocess.Popen([self.copy] + self.args,
                                      stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE)

    def stop(self):
        if self.child is None:
            return
        try:
            self.child.stdin.close()
        except Exception:
            pass
        try:
            self.child.wait(timeout=5)
        except Exception:
            self.child.kill()
            try:
                self.child.wait(timeout=5)
            except Exception:
                pass
        self.child = None

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

    def start_pump(self):
        """Пересылает ответы эмулятора клиенту, дописывая NOTE после перезапуска."""
        child = self.child
        generation = self.generation

        def run():
            for line in child.stdout:
                self.write_out(self.annotate(line))
            # Эмулятор закончился сам по себе - клиенту тут помочь нечем
            if generation == self.generation:
                os._exit(0)

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
            content.append({"type": "text", "text": NOTE})
            return (json.dumps(message, ensure_ascii=False) + "\n").encode("utf-8")
        except Exception:
            return line

    # ------------------------------------------------------------ перезапуск

    def restart(self):
        """Поднимает новый эмулятор и повторяет ему рукопожатие клиента."""
        log("сборка обновилась, перезапускаю эмулятор")
        self.generation += 1
        self.stop()
        self.spawn()

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
        self.spawn()
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
            elif method == "tools/call" and self.rebuilt():
                try:
                    self.restart()
                    self.note_for = request_id
                except Exception as error:
                    log("перезапуск не удался: %s" % error)
                    os._exit(1)

            try:
                self.to_child(line)
            except Exception:
                break

        # Тем же счётчиком глушится и поток пересылки: иначе он увидит конец
        # вывода эмулятора, решит, что тот умер сам, и снимет процесс до уборки
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
