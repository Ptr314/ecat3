# Orion-128

[Русский](orion-128.md) | [Български](orion-128.bg.md)

A hobbyist computer («Radio» magazine, 1990). The «Орион-128» group in the machine selection window.

## Configurations

| Configuration | Processor | Contents | Description |
|---|---|---|---|
| КР580ВМ80 / Монитор-1 | KR580VM80A, 2.5 MHz | The original magazine version, Monitor-1, tape recorder | [Orion-128-m1.en.md](../../deploy/computers/orion-128/Orion-128-m1.en.md) |
| КР580ВМ80 / Монитор-3 | KR580VM80A | Monitor-3, ROM disk, ORDOS, floppy drives (CP/M) | [Orion-128-m3.en.md](../../deploy/computers/orion-128/Orion-128-m3.en.md) |
| Z80 Card II | Z80 | «Leningrad version», boots CP/M with the BP shell | [Orion-128-Z80.en.md](../../deploy/computers/orion-128/Orion-128-Z80.en.md) |
| Z80 Card II / Zexall | Z80 | The ZEXALL test from a floppy | [Orion-128-Z80-zexall.en.md](../../deploy/computers/orion-128/Orion-128-Z80-zexall.en.md) |
| Z80 Card II / Тест Чистякова | Z80 | A test of the Z80 Card II board | [Orion-128-tZ80.en.md](../../deploy/computers/orion-128/Orion-128-tZ80.en.md) |

Debug configurations (a checkbox in the machine selection window): «Монитор-1 + Z80», «Тест памяти» (memory test).

## What is needed

The ROMs (monitors, ROM disks) come with the emulator: `deploy/computers/orion-128/files/`. Floppies and programs are in `software/orion-128`.

## Files

* Programs: `.rko`, `.bru`, `.ord` &ndash; loaded into memory (M1) or into the ORDOS RAM disk (M3) with «File / Open».
* Tape recorder: `.rko`.
* Floppies: `.odi`.

## Limitations

* Monitor-1: after writing to tape, the emulator's РУС/ЛАТ (Cyrillic/Latin) state no longer matches the monitor's; two presses of РУС/ЛАТ cure it.
* Monitor-1: loading ORDOS from the ROM disk with the directive R does not work.
