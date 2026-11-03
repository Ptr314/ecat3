# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Прогон eCat3-headless над сгенерированным сценарием .ecat: звук пишется в WAV
(COMMAND sound.record) на скорости MAX, так что минуты музыки считаются за
секунды, а LOG и снимки дают состояние машины.

Эмулятор запускается в своем рабочем каталоге (--workdir): там ссылки на
deploy/computers, data, software и копия deploy/.ecat.ini, так что рабочий
deploy/ecat.ini разработчика не читается и не переписывается.
"""

import os
import re
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
DEPLOY = os.path.join(ROOT, "deploy")


def find_exe():
    exe = os.environ.get("ECAT3_EXE")
    if exe and os.path.isfile(exe):
        return exe
    for d in ("cmake-build-headless", "cmake-build-mcp", "cmake-build-headless-mcp"):
        p = os.path.join(ROOT, "src", d, "eCat3-headless.exe" if os.name == "nt" else "eCat3-headless")
        if os.path.isfile(p):
            return p
    sys.exit("eCat3-headless не найден: соберите его или задайте ECAT3_EXE")


def environment(exe):
    """PATH с рантаймом компилятора, как в tests/run_tests.py"""
    env = dict(os.environ)
    if os.name != "nt":
        return env
    cache = os.path.join(os.path.dirname(exe), "CMakeCache.txt")
    extra = []
    if os.path.isfile(cache):
        text = open(cache, encoding="utf-8", errors="replace").read()
        m = re.search(r"^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$", text, re.M)
        if m:
            extra.append(os.path.dirname(m.group(1)).replace("/", os.sep))
    extra = [p for p in extra if os.path.isdir(p)]
    if extra:
        env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    return env


def workdir(base):
    """Рабочий каталог эмулятора: ссылки на каталоги deploy и свой ecat.ini"""
    wd = os.path.join(os.path.normpath(os.path.abspath(base)), "work")
    os.makedirs(wd, exist_ok=True)
    for name in ("computers", "data", "software"):
        link = os.path.join(wd, name)
        if os.path.exists(link):
            continue
        target = os.path.join(DEPLOY, name)
        if os.name == "nt":
            subprocess.run(["cmd", "/c", "mklink", "/J", link, target],
                           check=True, stdout=subprocess.DEVNULL)
        else:
            os.symlink(target, link)
    with open(os.path.join(DEPLOY, ".ecat.ini"), "rb") as f:
        ini = f.read()
    with open(os.path.join(wd, "ecat.ini"), "wb") as f:
        f.write(ini)
    return wd


def run(lines, name, outdir, timeout=900, exe=None):
    """Пишет outdir/name.ecat, гонит его и возвращает напечатанное"""
    outdir = os.path.normpath(os.path.abspath(outdir))
    os.makedirs(outdir, exist_ok=True)
    exe = exe or find_exe()
    path = os.path.join(outdir, name + ".ecat")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    wd = workdir(outdir)
    stamped = re.compile(re.escape(name) + r"-\d{4}(-\d{2}){5}\.log$")
    for f in os.listdir(outdir):
        if stamped.match(f):
            os.remove(os.path.join(outdir, f))
    proc = subprocess.run([exe, "--no-sound", "--workdir", wd, "--script", path],
                          env=environment(exe), timeout=timeout,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out = proc.stdout.decode("utf-8", errors="replace")
    err = proc.stderr.decode("utf-8", errors="replace").strip()
    if proc.returncode != 0 or err:
        sys.stderr.write(out[-3000:] + "\n" + err + "\n")
        raise RuntimeError("%s: код %d" % (name, proc.returncode))
    # LOG пишется в лог сценария: имя-дата.log рядом с ним
    for f in sorted(os.listdir(outdir)):
        if stamped.match(f):
            with open(os.path.join(outdir, f), encoding="utf-8", errors="replace") as lf:
                out += lf.read()
    return out


def load_wav(path):
    """(частота, отсчеты float в -1..1)"""
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        n = w.getnframes()
        ch = w.getnchannels()
        data = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        data = data.reshape(-1, ch)[:, 0]
    return rate, data


def db(x):
    return 20 * np.log10(max(float(x), 1e-9))
