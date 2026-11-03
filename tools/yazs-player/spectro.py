# SPDX-License-Identifier: GPL-3.0-or-later
# Part of the eCat3 project: https://github.com/Ptr314/ecat3
"""
Спектрограмма WAV (частота по логарифму, ноты по сетке) - посмотреть, что
звучит, без прослушивания.

    python spectro.py файл.wav начало_с конец_с выход.png
"""

import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt   # noqa: E402
import numpy as np                # noqa: E402

import render                     # noqa: E402


def main():
    wav, t0, t1, png = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    rate, x = render.load_wav(wav)
    x = x[int(t0 * rate):int(t1 * rate)]
    n = 4096
    hop = 512
    win = np.hanning(n)
    frames = 1 + (len(x) - n) // hop
    spec = np.array([np.abs(np.fft.rfft(x[i * hop:i * hop + n] * win)) for i in range(frames)]).T
    f = np.fft.rfftfreq(n, 1.0 / rate)
    # логарифмическая сетка 40 Гц - 8 кГц, по 1/4 полутона
    grid = 40.0 * 2 ** (np.arange(0, 12 * np.log2(8000 / 40.0) * 4) / 48.0)
    idx = np.searchsorted(f, grid).clip(1, len(f) - 1)
    img = 20 * np.log10(spec[idx] + 1e-6)
    img -= img.max()
    fig, ax = plt.subplots(figsize=(16, 7), dpi=80)
    ax.imshow(img, aspect="auto", origin="lower", vmin=-60, vmax=0, cmap="magma",
              extent=[t0, t1, 0, len(grid)])
    ticks = []
    labels = []
    for octv in range(1, 9):
        for name, semi in (("C", 0), ("A", 9)):
            fr = 440 * 2 ** ((octv - 4) + (semi - 9) / 12.0)
            if 40 <= fr <= 8000:
                ticks.append(48 * 12 * np.log2(fr / 40.0) / 12)
                labels.append("%s%d" % (name, octv))
    ax.set_yticks(ticks)
    ax.set_yticklabels(labels)
    ax.set_xlabel("с")
    ax.grid(axis="y", color="white", alpha=0.2)
    fig.tight_layout()
    fig.savefig(png)


if __name__ == "__main__":
    main()
