# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Послушать часть без настоящего времени: диск с одной мелодией, эмулятор
играет ее на скорости MAX и пишет WAV, потом - громкость по секундам, пики,
тишина, потерянные тики, по желанию MP3.

    python listen.py имя_модуля [--mp3] [--out каталог]
"""

import argparse
import os
import shutil
import subprocess
import sys

import numpy as np

import build
import mkdisk840
import render


def make_disk(module, outdir):
    song = build.load_score(module)
    data = song.build()
    player = build.assemble(outdir)
    disk = mkdisk840.Disk840(open(build.TEMPLATE, "rb").read())
    disk.delete("SOUNDTEST")
    disk.add("ЯЗС-ПЛЕЕР", mkdisk840.T_BIN, mkdisk840.b_body(build.PLAYER_ADDR, player), locked=True)
    disk.add(module.upper().replace("_", "-") + ".YZ", mkdisk840.T_BIN,
             mkdisk840.b_body(build.SONG_ADDR, data))
    path = os.path.join(outdir, module + ".dsk")
    open(path, "wb").write(bytes(disk.d))
    open(os.path.join(outdir, module + ".yz"), "wb").write(data)
    return path, song


def trim(wav):
    """Тишину до первого звука срезать (запись начата до клавиши)"""
    import wave
    rate, x = render.load_wav(wav)
    nz = np.nonzero(np.abs(x) > 0.002)[0]
    start = max(0, nz[0] - rate // 20) if len(nz) else 0
    with wave.open(wav, "rb") as w:
        params = w.getparams()
        w.setpos(start)
        frames = w.readframes(params.nframes - start)
    with wave.open(wav, "wb") as w:
        w.setparams(params)
        w.writeframes(frames)
    return start / float(rate)


def analyse(wav, secs):
    rate, x = render.load_wav(wav)
    n = len(x) // rate
    rms = [float(np.sqrt(np.mean(x[i * rate:(i + 1) * rate] ** 2))) for i in range(n)]
    peak = float(np.max(np.abs(x))) if len(x) else 0
    loud = float(np.mean(np.abs(x) > 0.89))
    print("длина %.1f с (ждали %d), пик %.2f (%.1f дБ), выше 0.89: %.3f%%" % (
        len(x) / float(rate), secs, peak, render.db(peak), 100 * loud))
    line = []
    for i, r in enumerate(rms):
        line.append("%3d" % int(round(-render.db(r))) if r > 0 else "  -")
        if len(line) == 20:
            print("  %3d-%3d с, -дБ: %s" % (i - 19, i, " ".join(line)))
            line = []
    if line:
        print("  %3d-    с, -дБ: %s" % (n - len(line), " ".join(line)))
    quiet = [i for i, r in enumerate(rms) if render.db(r) < -45]
    if quiet:
        print("тихие секунды (< -45 дБ):", quiet[:40])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module")
    ap.add_argument("--out", default=os.path.join(render.HERE, "out", "listen"))
    ap.add_argument("--mp3", action="store_true")
    ap.add_argument("--start", type=int, default=0, help="начать с кадра (клавиша ВПРАВО)")
    a = ap.parse_args()
    outdir = os.path.abspath(a.out)
    os.makedirs(outdir, exist_ok=True)
    disk, song = make_disk(a.module, outdir)
    secs = song.stats["seconds"]
    wav = a.module + ".wav"
    lines = [
        'MACHINE "agat/Agat-9-yazs.ext"',
        'COMMAND fdd0.load("%s")' % disk.replace("\\", "/"),
        "RESET cold",
        "SPEED MAX",
        "WAIT 12000",
        "LOGDEFS 16,10",
        'COMMAND sound.record("%s")' % wav,
        "KEY 100,100,ret",
        "WAIT 700",
        "LOG yazs.irqs",
        "LOG ram.value($1E,$1F)",
        "WAIT %d" % (max(1, secs - 1) * 1000),
        "LOG yazs.irqs",
        "LOG ram.value($1E,$1F)",
        "LOG ram.value($79,$7C)",
        "WAIT 3000",
        "COMMAND sound.record()",
        'SCREEN "%s.png"' % a.module,
        "EXIT",
    ]
    out = render.run(lines, "listen_" + a.module, outdir, timeout=3600)
    nums = []
    for l in out.splitlines():
        if l.startswith("[yazs.irqs]"):
            nums.append(int(l.split()[-1]))
        elif l.startswith("[ram.value]"):
            v = [int(x) for x in l.split(":")[1].split()]
            if len(v) == 4:
                print("запас тика: %d проходов ожидания (~%d тактов), тик %d" % (
                    v[0] + 256 * v[1], 22 * (v[0] + 256 * v[1]), v[2] + 256 * v[3]))
            else:
                nums.append(v[0] + 256 * v[1])
    if len(nums) == 4:
        lost = (nums[2] - nums[0]) - (nums[3] - nums[1])
        print("тиков %d, взведений таймера %d, потеряно %d" % (
            nums[3] - nums[1], nums[2] - nums[0], lost))
    print(song.stats)
    print("срезано тишины в начале: %.2f с" % trim(os.path.join(outdir, wav)))
    analyse(os.path.join(outdir, wav), secs)
    if a.mp3:
        ff = shutil.which("ffmpeg") or os.path.join("C:\\DEV", "ffmpeg-n9.0-latest-win64-lgpl-9.0", "bin", "ffmpeg.exe")
        mp3 = os.path.join(outdir, a.module + ".mp3")
        subprocess.run([ff, "-y", "-loglevel", "error", "-i", os.path.join(outdir, wav),
                        "-codec:a", "libmp3lame", "-b:a", "192k", mp3], check=True)
        print("mp3:", mp3)


if __name__ == "__main__":
    main()
