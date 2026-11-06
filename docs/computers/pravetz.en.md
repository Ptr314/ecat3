# Pravetz-82, 8M, 8A, 8C

[Русский](pravetz.md) | [Български](pravetz.bg.md) | Pravetz-8D &ndash; [separate page](pravetz-8d.en.md)

Bulgarian computers of the Pravetz combine, compatible with the Apple ][+ and //e. The Pravetz-8A was also made in Tashkent (СП «Вариант», the Variant joint venture) for schools of the USSR. The «Правец» group in the machine selection window.

## Configurations

| Configuration | Counterpart | Contents | Description |
|---|---|---|---|
| Правец-82 | Apple ][+ | 6502, 48 KB, 12 KB ROM, 128 KB RAM card in slot 0, 140 KB floppy drive in slot 6, tape recorder | [Pravetz-82.en.md](../../deploy/computers/pravetz/Pravetz-82.en.md) |
| Правец-8М | Apple ][+ | 64 KB, own ROM, character generator with four sets, floppy drive, tape recorder | [Pravetz-8M.en.md](../../deploy/computers/pravetz/Pravetz-8M.en.md) |
| Правец-8А | Apple //e | SM630 (NMOS 6502), 50 Hz, 64 KB + MEGARAM 64 KB, 140 KB floppy drive in slot 6 | [Pravetz-8A.en.md](../../deploy/computers/pravetz/Pravetz-8A.en.md) |
| Правец-8А (все платы) | Apple //e | MEGARAM 1 MB, printer cards in slots 1 and 2, U-LAN in slot 4, Liberty Drive in slot 5, COMport in slot 7 | [Pravetz-8A-full.en.md](../../deploy/computers/pravetz/Pravetz-8A-full.en.md) |
| Правец-8С | Apple //e | 60 Hz, 128 KB, built-in floppy drive, Centronics, RS-232 (8250), RGB; COMport in slot 5 | [Pravetz-8C.en.md](../../deploy/computers/pravetz/Pravetz-8C.en.md) |

The extension editor offers:

| Parameter | Machines |
|---|---|
| KNGMD (floppy controller) loader: 16 sectors (DOS 3.3) or 13 (DOS 3.2) | 82, 8A, 8C |
| Keyboard: early (RPT, no auto-repeat) or late (arrows, auto-repeat) | 82 |
| Character generator firmware: USSR, Bulgarian, СП «Вариант» | 8A, 8C |
| Jumper X20: the Pravetz set or the Apple //e set | 8A, 8C |
| Cyrillic layout: ГОСТ (the Soviet standard), БДС (the Bulgarian standard), ЯВЕРТЪ (phonetic) | 8A, 8C |
| ROM D38: factory or with the E8 patch | 8C |
| Firmware of the printer cards, U-LAN, Liberty Drive; COMport images D1&ndash;D9 | 8A (all cards), 8C |

Monitor (the «Video output» list on the toolbar, 8A and 8C): green, white, amber, RGB with the Apple colours, RGB of СП «Вариант», the stock Pravetz RGB.

## Differences from the Apple

### Pravetz-82 and Pravetz-8M (Apple ][+)

| | Apple ][+ | Pravetz-82 | Pravetz-8M |
|---|---|---|---|
| RAM on the main board | 48 KB | 48 KB | 64 KB |
| Slot 0 | 16 KB language card | 128 KB RAM card (eight language card banks, Saturn 128K registers); the first machines: a 16 KB RAM expansion card | &ndash; |
| ROM | Apple | modified, 12 KB | own |
| Character generator | Latin | Cyrillic (capitals only) in place of lowercase Latin, letters at the bottom of the character cell, Ю Ш Щ Ч on the codes @ [ ] ^; the left dot of a character in bit 6 | four sets of 2 KB, by default the one compatible with the 82 |
| Keyboard | QWERTY | ЯВЕРТЪ; two ЛАТ (Latin) keys instead of Shift and Caps Lock, МК &ndash; Control, ОСВ &ndash; Esc; early: RPT and one arrow, late: arrows and auto-repeat | ЯВЕРТЪ on the codes of the 8A, DEL key, two LEDs |
| Other | &ndash; | &ndash; | built-in CP/M card with a Z80 (not emulated) |

### Pravetz-8A and Pravetz-8C (Apple //e)

| | Apple //e | Pravetz-8A, 8C |
|---|---|---|
| Processor | 6502 (65C02 on the Enhanced //e) | SM630, NMOS 6502 |
| Scan | 60 Hz (50 Hz on the European //e) | 8A: 50 Hz, 312 lines, 14250.45 kHz crystal; 8C: 60 Hz, 262 lines, 14.31818 MHz crystal |
| ROM | Apple, 16 KB | own, 16 KB: Monitor with a mini-assembler and a disassembler; BASIC with hexadecimal numbers (`PRINT $20+$B`), `SETMOD`, `LINE INPUT`, `CONV`, formatted output; no tape commands (LOAD, SAVE, SHLOAD); its own code at $C400&ndash;$C7FF |
| Code table | one, ASCII | MODE0 (7-bit, Cyrillic at $E0&ndash;$FE, capitals only) and MODE1 (8-bit, capital and lowercase Latin and Cyrillic, `SETMOD 1`); table flag &ndash; a write to $C060, eighth bit of the key code &ndash; a read of $C060 |
| Character generator | MouseText | Cyrillic instead of MouseText and flashing characters; two sets, selected by the jumper X20; ALTCHARSET is also switched by a read of $C00E/$C00F |
| Keyboard | QWERTY | SM650 microcomputer, layouts ГОСТ (ЙЦУКЕН), БДС, ЯВЕРТЪ; C/L Lock key; F1 and F2 wired to the joystick buttons ($C061, $C062) |
| Extra memory | 64 KB (Extended 80-Column Card) | 8A: MEGARAM up to 1 MB in 64 KB pages, page number &ndash; a write to $C073 (as on the RamWorks); 8C: 128 KB on the main board |
| Monitor | colour (NTSC) | 8A: monochrome VMM 3107 (green), colour &ndash; through the RGB card in slot 7; 8C: built-in RGB output |
| Built-in devices | &ndash; | 8C: floppy controller, Centronics ($C100), RS-232 on an 8250 ($C200, registers $C098&ndash;$C09F), programs in the ROM D38; slots 4, 5, 7 free |
| Chips | MMU, IOU | SM631 (memory), SM632 (soft switches, keyboard) |

## What you need

The ROMs of the machines, controllers and cards come with the emulator: `deploy/computers/pravetz/files/`. By default the drive holds the school disk СПТУ-20 with a menu. Disks are in `software/pravetz`; games are in the software library on the emulator's site (https://ecat.emuverse.ru/library/pravetz/):

| File | Contents |
|---|---|
| `sptu20.dsk` | School disk СПТУ-20, DOS 3.3, menu |
| `p8ru01.dsk`, `p8ru02.dsk` | School disks; `p8ru02.dsk` has the VIRD virtual disk for MEGARAM |
| `sokoban.dsk` | «Сокобан» (Sokoban), NMOS processor only; in the library on the site |
| `arkanoid.dsk` | «Арканоид» (Arkanoid); in the library on the site |
| `igra-i-matematika.nib` | «Игра и математика» (Game and Mathematics); works only when write-protected; in the library on the site |
| `liberty-system.img` | Liberty Drive disk: ProDOS, XONEX SHELL, LIBERTY, ПАРИС |

COMport card: image 1 must hold a DOS prepared for the card (disk 1 of the COMPORT package).

## Files

| Medium | Format |
|---|---|
| 140 KB floppy drive (Disk II) | `.dsk`, `.do`, `.po`, `.nib`, `.nic`, `.mfm`, `.hfe`, `.woz` (WOZ 1, 2, 2.1), `.d13`; also saved to `.woz` and `.d13` |
| Liberty Drive | `.img`, 400 KB (40 tracks, 2 sides, 10 sectors of 512 bytes) |
| Tape recorder (82, 8M) | `.ptp`, `.bin`, `.wav`; a recording is saved to `.ptp` and `.wav` |

## Keyboard

C/L Lock (ЛАТ, КИР) &ndash; F10, RESET &ndash; F12 (with Ctrl), RPT (the early keyboard of the 82) &ndash; F11, F1 and F2 (8A) &ndash; F1 and F2. The on-screen keyboard matches the selected layout.

## Limitations

* Pravetz-8M: the CP/M card (Z80) is not emulated.
* U-LAN: the line self test ends with the message «ДЕФЕКТ #1» (defect #1); the answering part is not emulated.
* a2audit test: differences only where the Pravetz differs from the //e (ROM $C400&ndash;$C7FF, reading ALTCHAR).

## Sources

* [agatcomp.ru](https://agatcomp.ru/Pravetz/index.shtml) &ndash; programs and descriptions.
* P. Petrov, «Персональный компьютер Правец-8А» (The Pravetz-8A Personal Computer), Sofia, «Техника» (Tekhnika), 1989.
