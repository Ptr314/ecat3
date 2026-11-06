# Elektronika BK

[Русский](bk.md) | [Български](bk.bg.md)

Home computers built on the K1801VM1 processor (PDP-11 instruction set). The «БК» group in the machine selection window. Numbers in configurations and scripts are octal.

## Configurations

| Configuration | Contents | Description |
|---|---|---|
| БК0010 (Фокал) | 3 MHz, 16+16 KB of RAM, Monitor, FOCAL, tests; membrane keyboard | [BK-0010.en.md](../../deploy/computers/bk/BK-0010.en.md) |
| БК0010-01 (Бейсик) | 3 MHz, 16+16 KB of RAM, Monitor, BASIC «Вильнюс» (Vilnius) | [BK-0010-01.en.md](../../deploy/computers/bk/BK-0010-01.en.md) |
| БК0010-01 + КНГМД | K1801VP1-128 floppy controller, DISK 326 ROM, 16 KB of RAM in place of BASIC; ANDOS 3.30 | [BK-0010-01-fdd.en.md](../../deploy/computers/bk/BK-0010-01-fdd.en.md) |
| БК0010-01 + А16М | A16M controller (AltPro): floppy drive, ROM v2.44, 16 KB of RAM | [BK-0010-01-a16m.en.md](../../deploy/computers/bk/BK-0010-01-a16m.en.md) |
| БК0010-01 + СМК-512 | SMK-512 board (FPGA replica): 512 KB, BIOS v2.05, floppy drive, IDE hard disk | [BK-0010-01-smk512.en.md](../../deploy/computers/bk/BK-0010-01-smk512.en.md) |
| БК0011М (Бейсик) | 4 MHz, 128 KB of RAM in pages, BOS, BASIC, 16 palettes, frame interrupt | [BK-0011M.en.md](../../deploy/computers/bk/BK-0011M.en.md) |
| БК0011М + КНГМД | Floppy drives, ANDOS 3.30 | [BK-0011M-fdd.en.md](../../deploy/computers/bk/BK-0011M-fdd.en.md) |
| БК0011М + А16М | A16M controller, access to the console mode | [BK-0011M-a16m.en.md](../../deploy/computers/bk/BK-0011M-a16m.en.md) |
| БК0011М + СМК-512 | SMK-512 board | [BK-0011M-smk512.en.md](../../deploy/computers/bk/BK-0011M-smk512.en.md) |
| БК0011М + МСТД | Test diagnostics module, starts in the test menu | [BK-0011M-mstd.en.md](../../deploy/computers/bk/BK-0011M-mstd.en.md) |

Debug configurations: «Тест К1801ВМ1» (K1801VM1 test), «Тест двух процессоров» (two-processor test).

Chosen on the toolbar:
* Sound card on port 0177714: AY-3-8910, 2xAY (TurboSound, GryphonSound) or Covox.
* УП connector (user port): joystick, UVK-01 "Marsianka" mouse, printer or nothing.
* Screen mode: colour (RGB), «B/W», «Mono». The «Display / High precision» menu item &ndash; each line is drawn behind the beam.

## What is needed

The ROMs come with the emulator: `deploy/computers/bk/files/`. The ANDOS 3.30 floppy is `software/bk/andos330.img`. A hard disk image for the SMK-512 does not come with the emulator.

## Files

| Medium | Format |
|---|---|
| Programs | `.bin` with a load address header, «File / Open...» |
| Tape recorder | `.bin`, `.cod`, `.foc`, `.asc` (BASIC text) |
| Floppies | `.img`, `.bkd`, `.dsk` (819200 bytes; 40 tracks are accepted), `.hfe`. Writes go to memory, saving is by hand |
| Hard disk (SMK-512) | `.img`, `.hdi`; writes go straight to the file |

## Keyboard

РУС/ЛАТ (Russian/Latin) &ndash; Caps Lock, АР2 (alternate register 2) &ndash; Alt, СУ &ndash; Ctrl, СБР (clear) &ndash; Home. Letter case is set by the ЗАГЛ (capitals) and СТР (lower case) latches. Joystick: arrows, End, Home, PgUp, PgDn.
