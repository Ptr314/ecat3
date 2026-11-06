# Pravetz-8D

[Русский](pravetz-8d.md) | [Български](pravetz-8d.bg.md) | Pravetz-82, 8M, 8A, 8C &ndash; [separate page](pravetz.en.md)

A Bulgarian home computer of the mid-1980s, a counterpart of the Oric Atmos. It is not compatible with the Apple-compatible Pravetz models. The «Правец» group in the machine selection window, configuration «Правец-8Д».

## Configuration

| Unit | Contents |
|---|---|
| Processor | 6502, 1 MHz |
| Memory | 64 KB RAM, 16 KB ROM (Oric BASIC with Cyrillic) |
| Screen | ULA: 40x28 text, HIRES 240x200 with three lines of text, 8 colours, flashing, double height |
| Sound | AY-3-8912 |
| Ports | VIA MOS 6522 |
| Tape recorder | CSAVE/CLOAD, the machine controls the motor |
| Floppy drive | Pravetz controller (Disk II at $0310&ndash;$031F, 8DOS ROM), 140 KB; booting with `CALL 800` |

## Differences from the Oric Atmos

| | Oric Atmos | Pravetz-8D |
|---|---|---|
| ROM | Oric BASIC 1.1 | Oric BASIC with Cyrillic |
| Keyboard | Latin | C/L switch; Bulgarian alphabet (Ъ on the Y key, no Ы); Ю Ш Э Щ Ч &ndash; Shift with @ [ \ ] ^ |
| Floppy drive | Microdisc (3") | Pravetz controller: Disk II at $0310&ndash;$031F, 8DOS ROM (512 bytes, window $0320&ndash;$03FF), 5.25" 140 KB disks, 8D RDOS system |

Description: [Pravetz-8D.en.md](../../deploy/computers/pravetz/Pravetz-8D.en.md).

## What you need

The ROMs of the machine and the floppy controller come with the emulator: `deploy/computers/pravetz/files/`. By default the drive holds the 8D RDOS 2.31 system disk (`software/pravetz/rdos231-8d.dsk`, an empty catalogue; the commands `DIR`, `SAVE"ИМЯ"`, `LOAD"ИМЯ"`, where ИМЯ is the name).

## Files

| Medium | Format |
|---|---|
| Tape recorder | `.tap` (the format of Oric emulators), `.wav`; a recording is saved to `.tap` |
| Floppy drive | 140 KB images |

## Keyboard

* C/L (Cyrillic/Latin) &ndash; Caps Lock. In Cyrillic, Shift with a letter gives Latin.
* Ю, Ш, Э, Щ, Ч &ndash; the keys `` ` ``, `{`, `|`, `}`, `~`.
* Bulgarian alphabet: Ъ on the Y key, no letter Ы.
* МК &ndash; Ctrl, ОСВ &ndash; Esc, DEL &ndash; Backspace or Delete.
* RESET &ndash; F12: NMI, a warm start without losing the program.
* The on-screen keyboard is drawn from a photograph of the machine.
