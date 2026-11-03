# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Сборка диска ЯЗС-плеера: таблицы и строки -> gen_*.a65, AS65 -> плеер,
партитуры score/*.py -> *.YZ, все на копию soundtest.DSK (дорожка 0 с
«Быстропускалкой» и ДОС), образ - в library/agat/yazs-player.dsk.

    python build.py [--out каталог] [--only имя] [--no-hfe]
"""

import argparse
import importlib.util
import os
import subprocess
import sys

import mkdisk840
import tables

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
AS65 = os.path.join(ROOT, "src", "tests", "6502", "as65", "as65.exe")
TEMPLATE = os.path.join(ROOT, "docs-external", "Agat", "sound", "soundtest.DSK")
LIBRARY = os.path.join(ROOT, "library", "agat")
PLAYER_ADDR = 0x1800
SONG_ADDR = 0x3F00

# Партитуры в порядке на диске: модуль score/<имя>.py, имя файла на диске
SONGS = [
    ("m1_toccata", "1.ТОККАТА И ФУГА.YZ"),
    ("m2_adagio", "2.АДАЖИО.YZ"),
    ("m3_scherzo", "3.СКЕРЦО-ВАЛЬС.YZ"),
    ("m4_blues", "4.БЛЮЗ 1987.YZ"),
    ("m5_synth", "5.ЭЛЕКТРИЧЕСКИЙ СОН.YZ"),
    ("m6_finale", "6.ФИНАЛ.YZ"),
    ("tour", "ТУР ПО ТЕМБРАМ.YZ"),
]

STRINGS = {
    "s_header": " ЯЗС-ПЛЕЕР     АГАТ-9 + ЯЗС 5/2 ",
    "s_choose": "ВЫБЕРИТЕ МЕЛОДИЮ:",
    "s_help1": " ВВЕРХ/ВНИЗ-ВЫБОР  ВВОД-ИГРАТЬ",
    "s_help2": " А-ВСЕ ПОДРЯД  Д-ПЕРЕЧИТАТЬ ДИСК",
    "s_help3": " ВЛЕВО/ВПРАВО-СЛОТ  РЕД-МОНИТОР",
    "s_slot": "ЯЗС В СЛОТЕ ",
    "s_slot2": "",
    "s_nofiles": "НА ДИСКЕ НЕТ ФАЙЛОВ *.YZ",
    "s_reading": "ЧТЕНИЕ КАТАЛОГА...",
    "s_diskerr": "ОШИБКА ЧТЕНИЯ ДИСКА",
    "s_loading": "ЗАГРУЗКА...",
    "s_loaderr": "ОШИБКА ЗАГРУЗКИ ФАЙЛА",
    "s_time": "ВРЕМЯ 00:00/00:00    КАДР",
    "s_drum6": "БАРАБАН",
    "s_drum7": "ТАРЕЛКА",
    "s_regs": "РЕГИСТРЫ КАНАЛОВ И ПЕРИОДЫ:",
    "s_playhelp": "РЕД-МЕНЮ ПРОБЕЛ-ДАЛЕЕ ВВОД-ВИД",
}


def db_lines(label, data, per=16):
    out = []
    for i in range(0, len(data), per):
        chunk = ",".join("$%02X" % b for b in data[i:i + per])
        out.append("%-12s db %s" % (label if i == 0 else "", chunk))
    if not data:
        out.append("%-12s" % label)
    return out


def gen_sources():
    lines = ["; Сгенерировано build.py - не править", ""]
    ptab = tables.period_table()
    lines.append("            align 256")
    lines += db_lines("vibtab", tables.vibrato_table())
    lines += db_lines("ptab_lo", [p & 255 for p in ptab])
    lines += db_lines("ptab_hi", [p >> 8 for p in ptab])
    lines += db_lines("div12", [n // 12 for n in range(96)])
    lines += db_lines("mod12", [n % 12 for n in range(96)])
    lines += db_lines("rnd_lo", [0] + [(1 << (o - 1)) & 255 for o in range(1, 8)])
    lines += db_lines("rnd_hi", [0] + [(1 << (o - 1)) >> 8 for o in range(1, 8)])
    lines += db_lines("rowlo", [(0x1000 + r * 64) & 255 for r in range(32)])
    lines += db_lines("rowhi", [(0x1000 + r * 64) >> 8 for r in range(32)])
    names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
    lines += db_lines("notenm", list(tables.agat("".join(names))))
    for k, v in STRINGS.items():
        if len(v) > 32:
            raise ValueError("строка %s длиннее 32: %d" % (k, len(v)))
        lines += db_lines(k, list(tables.agat(v)) + [0])
    # Заголовок колонок трекера: колонки trkx в tracker.a65
    cols = bytearray(tables.agat(" " * 64))
    for x, s in ((0, "СТР"), (5, "ТОН 1"), (14, "ТОН 2"), (23, "ТОН 3"), (32, "ТОН 4"),
                 (41, "ТОН 5"), (51, "Б6"), (56, "Т7")):
        cols[x:x + len(s)] = tables.agat(s)
    lines += db_lines("s_trkcols", list(cols.rstrip(b"\xa0")) + [0])
    with open(os.path.join(HERE, "gen_tables.a65"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(HERE, "gen_const.a65"), "w", encoding="utf-8", newline="\n") as f:
        f.write("; Сгенерировано build.py - не править\n")


def assemble(out):
    binf = os.path.join(out, "player.bin")
    lst = os.path.join(out, "player.lst")
    if os.path.exists(binf):
        os.remove(binf)
    proc = subprocess.run([AS65, "-l" + lst, "-n", "-w", "-h0", "-o" + binf, "player.a65"],
                          cwd=HERE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = proc.stdout.decode("cp866", errors="replace")
    if proc.returncode != 0 or not os.path.exists(binf) or "rror" in text:
        sys.stderr.write(text)
        sys.exit("AS65: ошибка сборки")
    data = open(binf, "rb").read()
    end = PLAYER_ADDR + len(data)
    if end > SONG_ADDR - 4:
        sys.exit("плеер %d байт заходит на мелодию ($%04X)" % (len(data), end))
    return data


def load_score(name):
    path = os.path.join(HERE, "score", name + ".py")
    spec = importlib.util.spec_from_file_location("score_" + name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.join(HERE, "score"))
    spec.loader.exec_module(mod)
    return mod.song()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--disk", default=os.path.join(LIBRARY, "yazs-player.dsk"))
    ap.add_argument("--only", action="append", help="только эти партитуры (имя модуля)")
    ap.add_argument("--extra", action="append", default=[], help="добавить модуль score/ на диск")
    ap.add_argument("--no-hfe", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    gen_sources()
    player = assemble(a.out)
    print("плеер: %d байт, $%04X-$%04X" % (len(player), PLAYER_ADDR, PLAYER_ADDR + len(player) - 1))

    disk = mkdisk840.Disk840(open(TEMPLATE, "rb").read())
    disk.delete("SOUNDTEST")
    disk.set_volume("ЯЗС-ПЛЕЕР")
    disk.add("ЯЗС-ПЛЕЕР", mkdisk840.T_BIN, mkdisk840.b_body(PLAYER_ADDR, player), locked=True)

    songs = [s for s in SONGS if not a.only or s[0] in a.only]
    songs += [(m, m.upper().replace("_", "-") + ".YZ") for m in a.extra]
    total = 0
    for module, fname in songs:
        if not os.path.exists(os.path.join(HERE, "score", module + ".py")):
            print("  (нет score/%s.py - пропуск)" % module)
            continue
        song = load_score(module)
        data = song.build()
        with open(os.path.join(a.out, module + ".yz"), "wb") as f:
            f.write(data)
        disk.add(fname, mkdisk840.T_BIN, mkdisk840.b_body(SONG_ADDR, data))
        st = song.stats
        total += st["seconds"]
        print("  %-24s %5d байт, %3d кадров, %3d паттернов, %d:%02d, speed %d, N %d" % (
            fname, st["bytes"], st["frames"], st["patterns"], st["seconds"] // 60,
            st["seconds"] % 60, st["speed"], st["n"]))
    print("всего %d:%02d" % (total // 60, total % 60))

    os.makedirs(os.path.dirname(a.disk), exist_ok=True)
    with open(a.disk, "wb") as f:
        f.write(bytes(disk.d))
    print("диск:", a.disk)
    for ftype, name, n in mkdisk840.Disk840(bytes(disk.d)).listing():
        print("  %02X %4d  %s" % (ftype, n, name))
    if not a.no_hfe:
        # .hfe для Gotek/HxC пишет сам эмулятор: тот же образ, дорожки Агата 840
        import render
        hfe = os.path.splitext(a.disk)[0] + ".hfe"
        if os.path.exists(hfe):
            os.remove(hfe)
        render.run(['MACHINE "agat/Agat-9-yazs.ext"',
                    'COMMAND fdd0.load("%s")' % os.path.abspath(a.disk).replace("\\", "/"),
                    'COMMAND fdd0.save("%s")' % os.path.abspath(hfe).replace("\\", "/"),
                    "EXIT"], "hfe", a.out)
        print("hfe: ", hfe, os.path.getsize(hfe), "байт")


if __name__ == "__main__":
    main()
