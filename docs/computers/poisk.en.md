# Poisk-1

[Русский](poisk.md) | [Български](poisk.bg.md)

An IBM PC compatible computer by the Elektronmash production association (Kyiv, 1989). The «Поиск» group in the machine selection window.

## Configurations

| Configuration | Contents | Description |
|---|---|---|
| Поиск-1 (512 КБ) | K1810VM88 at 5 MHz, 512 KB of RAM, B504 floppy cartridge (two 720 KB drives), tape recorder | [Poisk-1.en.md](../../deploy/computers/poisk/Poisk-1.en.md) |
| Поиск-1 (128 КБ) | 128 KB of RAM: the BIOS and cassettes work, MS-DOS 5.0 does not | [Poisk-1-128k.en.md](../../deploy/computers/poisk/Poisk-1-128k.en.md) |
| Поиск-1 (128+256 Кб) | Cartridge memory without wait states, 352 KB in all | [Poisk-1-cart-ram.en.md](../../deploy/computers/poisk/Poisk-1-cart-ram.en.md) |
| Поиск-1 (Быстрая загрузка) | The «Moscow 1991» floppy ROM: MS-DOS boots several times faster | [Poisk-1-fdd302.en.md](../../deploy/computers/poisk/Poisk-1-fdd302.en.md) |

Video: 320x200 (4 colours) and 640x200, no text mode; the BIOS draws text through an NMI trap.

## What is needed

The ROMs come with the emulator: `deploy/computers/poisk/files/` (the BIOS of 14.02.1991, the floppy cartridge ROM). Software is in `software/poisk`: `msdos5_vc405.img` (MS-DOS 5.0), `TETRIS.CAS`.

## Files

| Medium | Format |
|---|---|
| Floppies | `.img`, `.ima`, `.dsk` (sector dump, 720 KB) |
| Tape recorder | `.cas` (PCE format), reading and writing; a file without a header gets a header with the file name |

## Notes

* Without a floppy in A: the BIOS opens a menu after 15 seconds: F1 &ndash; cassette, F2 &ndash; ROM.
* The factory floppy driver: MS-DOS boots in about a minute, as on the real machine.
* INT 16h of the factory BIOS does not support the extended keyboard; Volkov Commander 4.99 does not see key presses.
* Latin/Cyrillic &ndash; F12, Ukrainian І &ndash; F11.
