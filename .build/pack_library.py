#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Упаковывает библиотеку программ (library/) в один zip для сайта.

    python .build/pack_library.py [архив.zip]

По умолчанию пишет .build/release/ecat3-library.zip. В архиве - каталог
library/ со всеми образами и их .lst; распакованный в корень репозитория, он
возвращает тестам их образы. README.md каталога в архив не идёт: он про
устройство репозитория. Конвертер wav2uknc.py лежит в tools/, а в архив кладётся
к образам УК-НЦ, куда на него ссылаются описания машин.

Тот же архив делает pack_library.cmd (Windows, 7-Zip): состав меняется в обоих.
"""

import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBRARY = os.path.join(ROOT, "library")
DEFAULT_OUT = os.path.join(ROOT, ".build", "release", "ecat3-library.zip")

# Файлы из других мест репозитория: имя в архиве -> путь от корня
EXTRA = {
    "library/uknc/wav2uknc.py": "tools/uknc-tape/wav2uknc.py",
}


def collect():
    entries = {}
    for dirpath, dirnames, filenames in os.walk(LIBRARY):
        dirnames.sort()
        for name in filenames:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            if rel == "library/README.md":
                continue
            entries[rel] = path
    for name, rel in EXTRA.items():
        entries[name] = os.path.join(ROOT, rel)
    return entries


def main():
    out = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    entries = collect()
    images = [n for n in entries if not n.endswith((".lst", ".py"))]
    if not images:
        sys.exit("В library/ нет образов: каталога нет в git, его наполняют из прежнего архива")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    tmp = out + ".tmp"
    raw = 0
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in sorted(entries):
            z.write(entries[name], name)
            raw += os.path.getsize(entries[name])
    os.replace(tmp, out)
    print("%s: %d файлов (%d образов), %.1f МБ -> %.1f МБ"
          % (out, len(entries), len(images), raw / 1e6, os.path.getsize(out) / 1e6))


if __name__ == "__main__":
    main()
