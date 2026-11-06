# Agat

[Русский](agat.md) | [Български](agat.bg.md)

A Soviet computer compatible with the Apple ][ in its processor and part of its architecture. The «Агат» group in the machine selection window.

## Configurations

| Configuration | Contents | Description |
|---|---|---|
| Агат-7 140 Кб | 32 KB on the main board, 32 KB of pseudo-ROM, 32 KB of additional RAM, 140 KB floppy drive; boots into IKP-7 | [Agat-7.en.md](../../deploy/computers/agat/Agat-7.en.md) |
| Агат-7 840 Кб | The same with an 840 KB floppy drive; a disk with games | [Agat-7-840.en.md](../../deploy/computers/agat/Agat-7-840.en.md) |
| Агат-7 840 Кб (ПП) | An 840 KB drive with IKP-7, the palette and loadable character generator card in slot 1 | [Agat-7-840-pp.en.md](../../deploy/computers/agat/Agat-7-840-pp.en.md) |
| Агат-7 Max | 64 KB on the main board, 128 KB each of pseudo-ROM and additional RAM, two 840 KB floppy drives | [Agat-7-max.en.md](../../deploy/computers/agat/Agat-7-max.en.md) |
| Агат-9 | 128 KB, 840 KB floppy drive; boots into IKP | [Agat-9.en.md](../../deploy/computers/agat/Agat-9.en.md) |
| Агат-9 (ПП) | The same with the palette card in slot 3 | [Agat-9-pp.en.md](../../deploy/computers/agat/Agat-9-pp.en.md) |
| Агат-9 (ЯЗС) | 840 and 140 KB floppy drives, the sound synthesis card 5/2 (YaZS) in slot 1 | [Agat-9-yazs.en.md](../../deploy/computers/agat/Agat-9-yazs.en.md) |

Agat-9: the Display menu offers a monochrome output, handling of the brightness bit and an experimental palette.

## What you need

The ROMs (monitors, character generators, controller boot loaders) come with the emulator: `deploy/computers/agat/files/`. The disks (IKP-7, games, tests, the YaZS demo) &ndash; in `software/agat`.

## Files

| Drive | Images |
|---|---|
| 140 KB | `.dsk`, `.do`, `.po`, `.nib`, `.nic`, `.mfm`, `.hfe`; 35&ndash;40 tracks |
| 840 KB | `.dsk`, `.aim`, `.hfe` |

Saving: 140 KB &ndash; `.dsk`, `.mfm`; 840 KB &ndash; `.dsk`.

## Keyboard

The right-hand function block of the Agat is the PC numeric keypad by position (Num Lock). Without a numeric keypad: «Settings / Keyboard» (arrows or U I O / J K L / M , .).
