# Irisha and clones

[Русский](irisha.md) | [Български](irisha.bg.md)

An educational computer built on the KR580VM80A, and its clones. The «Ириша» group in the machine selection window.

## Configurations

| Configuration | Contents | Description |
|---|---|---|
| Ириша (ГМД70) | The version from the «Микропроцессорные средства и системы» (Microprocessor Tools and Systems) magazine: 64+64 KB RAM, ROM extension (games, editor), tape recorder, 8" GMD7012 floppy drive | [Irisha-gmd70.en.md](../../deploy/computers/irisha/Irisha-gmd70.en.md) |
| Ириша (КНГМД) | Irisha-M with the RTV*001.89 module: KR1818VG93, BIOS 4U, CP/M, 5.25" drives A:, B:, RAM disk D:, 8" drives E:, F: | [Irisha-kngmd.en.md](../../deploy/computers/irisha/Irisha-kngmd.en.md) |
| Диалог | A clone with its own firmware (20 KB of ROM: menu, IRITEXT editor, DDT debugger), without additional RAM | [Dialog.en.md](../../deploy/computers/irisha/Dialog.en.md) |
| Каспий | A clone with its own firmware; the debugger is restored from the Irisha firmware | [Kaspiy.en.md](../../deploy/computers/irisha/Kaspiy.en.md) |

## What is needed

The ROMs come with the emulator: `deploy/computers/irisha/files/`. Floppies are in `software/irisha` (`disk5.gmd` &ndash; games, `02.cpm` &ndash; CP/M utilities).

## Files

| Medium | Format |
|---|---|
| 8" GMD7012 floppy drive | `.gmd`, 256 KB |
| 5.25" floppy drive (KNGMD) | 360 KB: `.cpm` (by sides), `.dsk` (by cylinders); the track order follows the extension |
| Tape recorder | `.bin` (data without headers) |

## Limitations

* Kaspiy: the network connection to the floppy drive through the US-10 is not emulated.
