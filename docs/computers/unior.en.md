# Unior and Argo

[Русский](unior.md) | [Български](unior.bg.md)

The Kyiv computers FV-6506 and FV-6511, a development of the Radio-86RK. The «Юниор» group in the machine selection window.

## Configurations

| Configuration | Components | Description |
|---|---|---|
| Юниор ФВ-6506 | KR580VM80A at 2.2 MHz, 128 KB + seven blocks of 64 KB, 80x25 colour screen (VG75), 240x128 pseudographics, KR580VI53 sound, 88-key keyboard | [Unior.en.md](../../deploy/computers/unior/Unior.en.md) |
| Арго ФВ-6511 | Z80, character generator in RAM, other port addresses; ZX Spectrum mode | [Argo.en.md](../../deploy/computers/unior/Argo.en.md) |

Both machines: a tape recorder with remote control (the machine controls the tape), the TCP/M system from tape (`I` in the monitor; the prompt after 2.5 minutes, at the speed of a real tape), an on-screen keyboard.

## What is needed

ROMs, character generators and palettes come with the emulator: `deploy/computers/unior/files/`. The cassettes are in `software/unior`, Pascal and SuperCalc are in the software library on the emulator's site (https://ecat.emuverse.ru/library/unior/); each cassette has a `.lst` list of its files:

* `elcon.a.bt` &ndash; TCP/M and games (inserted in the Unior);
* `argo.a.bt` &ndash; the Argo system cassette, `ZX.COM`;
* `pasc.a.bt` &ndash; Pascal/MT+ 5.2;
* `sc.a.bt` &ndash; SuperCalc 1.12;
* `blank.tap` &ndash; a blank ZX Spectrum tape.

## Files

| Medium | Format |
|---|---|
| Unior and Argo tape | `.bt` (timings and data of the records) |
| ZX Spectrum tape (Argo, ZX mode) | `.tap`, `.tzx` (standard-speed blocks); recording &ndash; `.tap` |

## ZX Spectrum mode (Argo)

Start: `B:`, then `ZX`. Ф10 (F10) turns the keyboard on. CAPS SHIFT &ndash; ДОП, SYMBOL SHIFT &ndash; Б, SPACE &ndash; Ю. The arrows control the tape recorder: up is play, down is stop, left is rewind, right is fast forward.
