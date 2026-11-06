# Radio-86RK and compatibles

[Русский](radio-86rk.md) | [Български](radio-86rk.bg.md)

A hobbyist computer («Radio» magazine, 1986) and its factory-made versions. The «Радио-86РК» group in the machine selection window.

## Configurations

| Configuration | Machine | Description |
|---|---|---|
| Радио-86РК/32K | The classic version, 32 KB of RAM | [Radio-86rk.en.md](../../deploy/computers/radio-86rk/Radio-86rk.en.md) |
| Спектр-001 | A counterpart of the Radio-86RK, differs in the monitor prompt line | [Spektr001.en.md](../../deploy/computers/radio-86rk/Spektr001.en.md) |
| Апогей-БК01Ц | Extended ROM, KR580VI53 timer, colour attributes, ROM disk | [Apogey.en.md](../../deploy/computers/apogey/Apogey.en.md) |
| Микроша | A factory-made version: sound on the KR580VI53, its own keyboard circuit | [Mikrosha.en.md](../../deploy/computers/mikrosha/Mikrosha.en.md) |

All machines: KR580VM80A, the KR580VG75 video controller and the KR580VT57 DMA controller (emulated cycle by cycle), a tape recorder, the on-screen keyboard.

## What is needed

The ROMs and character generators come with the emulator: `deploy/computers/radio-86rk/files/`, `apogey/files/`, `mikrosha/files/`. Programs are in `software/radio-86rk`, `software/apogey`, `software/mikrosha`.

## Files

| Machine | Programs (memory and tape) |
|---|---|
| Radio-86RK, Spektr-001 | `.rk`, `.rkr`, `.gam` |
| Apogey | `.rka` |
| Mikrosha | `.rkm` (its own tape speed, about 700 baud) |

What the machine records on tape is saved to a file with the machine's extension; on reading, the extension selects the format.

## Peculiarities

* Mikrosha: switching to the Russian mode produces a continuous tone, as on the real machine; any key stops it.
