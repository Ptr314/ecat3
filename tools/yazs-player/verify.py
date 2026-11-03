# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Проверка player.a65 по модели pyplay.py: эмулятор играет мелодию с диска,
сценарий снимает счетчик тиков плеера, его регистры и периоды каналов (из ОЗУ)
и регистры ячейки, а модель дает их же после того же числа тиков. Снимок мог
попасть внутрь тика (часть каналов уже следующего) - канал сходится, если
совпал с моделью после тика n или n+1.

    python verify.py диск.dsk файл.yz [снимков] [шаг_мс]
"""

import os
import re
import sys

import pyplay
import render

ADDR_TCNT = 0x1E
ADDR_LREG = 0x05E8
ADDR_PLO = 0x05F0
ADDR_PHI = 0x05F8


def script(disk, samples, step):
    lines = [
        'MACHINE "agat/Agat-9-yazs.ext"',
        'COMMAND fdd0.load("%s")' % disk.replace("\\", "/"),
        "RESET cold",
        "SPEED MAX",
        "WAIT 12000",
        "KEY 100,100,ret",
        "WAIT 800",
        "LOGDEFS 8,16",
    ]
    for _ in range(samples):
        lines += ["WAIT %d" % step,
                  "LOG ram.value($%X,$%X)" % (ADDR_TCNT, ADDR_TCNT + 1),
                  "LOG ram.value($%X,$%X)" % (ADDR_LREG, ADDR_LREG + 4),
                  "LOG ram.value($%X,$%X)" % (ADDR_PLO, ADDR_PLO + 4),
                  "LOG ram.value($%X,$%X)" % (ADDR_PHI, ADDR_PHI + 4),
                  "LOG yazs.regs($8,$E)"]
    lines.append("EXIT")
    return lines


def parse(out):
    vals = []
    for line in out.splitlines():
        if line.startswith("[ram.value]") or line.startswith("[yazs.regs]"):
            nums = re.findall(r"\$([0-9A-Fa-f]{2})\b", line.split(":", 1)[1])
            vals.append([int(x, 16) for x in nums])
    samples = []
    for i in range(0, len(vals) - 4, 5):
        t = vals[i][0] | (vals[i][1] << 8)
        samples.append({"tick": t, "lreg": vals[i + 1], "plo": vals[i + 2],
                        "phi": vals[i + 3], "regs": vals[i + 4]})
    return samples


def model(data, upto):
    p = pyplay.Player(data)
    snaps = {0: ([t.last_reg for t in p.tone], [t.last_per for t in p.tone])}
    while p.ticks < upto + 2 and p.step():
        snaps[p.ticks] = ([t.last_reg for t in p.tone], [t.last_per for t in p.tone])
    return snaps


def main():
    disk, yz = sys.argv[1], sys.argv[2]
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    step = int(sys.argv[4]) if len(sys.argv) > 4 else 97
    out = render.run(script(os.path.abspath(disk), n, step), "verify",
                     os.path.join(os.path.dirname(os.path.abspath(disk)), "verify"))
    samples = parse(out)
    data = open(yz, "rb").read()
    snaps = model(data, max(s["tick"] for s in samples) + 2)
    bad = 0
    checked = 0
    for s in samples:
        t = s["tick"]
        if t not in snaps:
            continue
        cands = [snaps[k] for k in (t, t + 1) if k in snaps]
        for c in range(5):
            per = s["plo"][c] | (s["phi"][c] << 8)
            per = -1 if per == 0xFFFF else per
            # внутри тика регистр канала может быть уже следующим, а период - еще нет
            ok = any(r[c] == s["lreg"][c] for r, p in cands) and any(p[c] == per for r, p in cands)
            # на ячейке - то же, что плеер считает записанным
            ok = ok and s["regs"][c] == s["lreg"][c]
            checked += 1
            if not ok:
                bad += 1
                if bad <= 20:
                    print("тик %d канал %d: плеер reg=%02X per=%d ячейка=%02X; модель %s" % (
                        t, c + 1, s["lreg"][c], per, s["regs"][c],
                        [("%02X" % r[c], p[c]) for r, p in cands]))
    print("снимков %d, проверено каналов %d, расхождений %d; тики %d..%d" % (
        len(samples), checked, bad, samples[0]["tick"], samples[-1]["tick"]))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
