# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Замер палитры ЯЗС на модели eCat3: все сочетания полос (НЧ/СЧ/ВЧ) x D3 x три
огибающие на нотах C2..C6 в канале 1 и все 24 варианта ударных. Регистры
пишутся сценарием (COMMAND yazs.set, запись по шине), звук - в WAV на скорости
MAX, потом считаются уровни, спектры и спады.

    python measure.py [каталог]       (по умолчанию ./out)
"""

import json
import os
import sys

import numpy as np

import render

CLOCK = 1021000
NOTES = [36, 48, 60, 72, 84]          # C2 C3 C4 C5 C6 (MIDI)
ENVS = [("D4", 0x10), ("D5", 0x20), ("D4D5", 0x30)]
ON_MS, OFF_MS = 600, 250


def period(midi):
    return int(round(CLOCK / (440.0 * 2 ** ((midi - 69) / 12.0))))


def script():
    lines = [
        'MACHINE "agat/Agat-9-yazs.ext"',
        "SPEED MAX",
        "WAIT 9000",
        # Счетчик тона 1: режим 3, младший и старший байт
        "COMMAND yazs.set($3,$36)",
        "COMMAND yazs.set($8,$00)",
        "COMMAND yazs.set($D,$00)",
        "COMMAND yazs.set($E,$00)",
        'COMMAND sound.record("palette.wav")',
        "WAIT 500",
    ]
    segs = []
    t = 500
    for bus in range(1, 8):
        for d3 in (0, 8):
            for ename, ebits in ENVS:
                for m in NOTES:
                    n = period(m)
                    lines.append("COMMAND yazs.set($0,$%02X)" % (n & 255))
                    lines.append("COMMAND yazs.set($0,$%02X)" % (n >> 8))
                    lines.append("COMMAND yazs.set($8,$%02X)" % (bus | d3 | ebits))
                    lines.append("WAIT %d" % ON_MS)
                    lines.append("COMMAND yazs.set($8,$%02X)" % (bus | d3))
                    lines.append("WAIT %d" % OFF_MS)
                    segs.append({"kind": "tone", "bus": bus, "d3": d3 >> 3, "env": ename,
                                 "note": m, "t": t, "on": ON_MS})
                    t += ON_MS + OFF_MS
    for reg, count in ((0xD, 16), (0xE, 8)):
        for v in range(count):
            val = v if reg == 0xD else ((v & 3) | ((v & 4) << 1))
            lines.append("COMMAND yazs.set($%X,$%02X)" % (reg, val))
            lines.append("WAIT 30")
            lines.append("COMMAND yazs.set($%X,$%02X)" % (reg, val | 0x10))
            segs.append({"kind": "drum", "ch": 6 if reg == 0xD else 7, "val": val,
                         "t": t + 30, "on": 1500})
            lines.append("WAIT 1500")
            lines.append("COMMAND yazs.set($%X,$%02X)" % (reg, val))
            t += 1530
    lines += ["WAIT 300", "COMMAND sound.record()", "EXIT"]
    return lines, segs


def analyse(rate, x, segs):
    out = []
    for s in segs:
        a = int(s["t"] * rate / 1000)
        b = int((s["t"] + s["on"]) * rate / 1000)
        seg = x[a:b]
        win = int(rate * 0.010)
        frames = len(seg) // win
        rms = np.sqrt(np.mean(seg[:frames * win].reshape(frames, win) ** 2, axis=1))
        env = [render.db(v) for v in rms]
        r = dict(s)
        r["peak_db"] = max(env)
        r["peak_ms"] = int(np.argmax(rms)) * 10
        if s["kind"] == "tone":
            # Уровень удержания - последние 200 мс ноты
            tail = seg[int(len(seg) - 0.2 * rate):]
            r["sustain_db"] = render.db(np.sqrt(np.mean(tail ** 2)))
            r["at300_db"] = env[min(29, len(env) - 1)]
            body = seg[int(0.05 * rate):int(0.35 * rate)]
        else:
            pk = int(np.argmax(rms))
            r["decay20_ms"] = next((10 * (i - pk) for i in range(pk, len(env))
                                    if env[i] < env[pk] - 20), None)
            r["decay40_ms"] = next((10 * (i - pk) for i in range(pk, len(env))
                                    if env[i] < env[pk] - 40), None)
            body = seg[:int(0.25 * rate)]
        spec = np.abs(np.fft.rfft(body * np.hanning(len(body))))
        f = np.fft.rfftfreq(len(body), 1.0 / rate)
        p = spec ** 2
        tot = p.sum() + 1e-30
        r["centroid"] = float((f * p).sum() / tot)
        r["low"] = float(p[f < 300].sum() / tot)
        r["mid"] = float(p[(f >= 300) & (f < 3000)].sum() / tot)
        r["high"] = float(p[f >= 3000].sum() / tot)
        r["fpeak"] = float(f[np.argmax(spec[1:]) + 1])
        out.append(r)
    return out


def main():
    outdir = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(render.HERE, "out"))
    lines, segs = script()
    render.run(lines, "palette", outdir, timeout=1800)
    rate, x = render.load_wav(os.path.join(outdir, "palette.wav"))
    res = analyse(rate, x, segs)
    with open(os.path.join(outdir, "palette.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    names = {1: "ВЧ", 2: "СЧ", 3: "ВЧ+СЧ", 4: "НЧ", 5: "НЧ+ВЧ", 6: "НЧ+СЧ", 7: "все"}
    print("Тоны: пик / уровень через 300 мс / удержание, дБ, по нотам C2..C6")
    for r in res:
        if r["kind"] != "tone" or r["note"] != NOTES[0]:
            continue
        row = [x for x in res if x["kind"] == "tone" and x["bus"] == r["bus"]
               and x["d3"] == r["d3"] and x["env"] == r["env"]]
        cells = " ".join("%5.1f/%5.1f/%5.1f" % (q["peak_db"], q["at300_db"], q["sustain_db"]) for q in row)
        print("%-6s D3=%d %-5s %s" % (names[r["bus"]], r["d3"], r["env"], cells))
    print("Ударные: пик дБ, спад -20/-40 мс, центр спектра Гц, доли <300/300-3k/>3k, пик Гц")
    for r in res:
        if r["kind"] == "drum":
            print("%d $%02X  %5.1f  %5s %5s  %6.0f  %.2f %.2f %.2f  %6.0f" % (
                r["ch"], r["val"], r["peak_db"], r["decay20_ms"], r["decay40_ms"],
                r["centroid"], r["low"], r["mid"], r["high"], r["fpeak"]))


if __name__ == "__main__":
    main()
