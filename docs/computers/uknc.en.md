# UKNC (Elektronika MS-0511)

[Русский](uknc.md) | [Български](uknc.bg.md)

An educational computer with two KM1801VM2 processors: the central one (8 MHz) and the peripheral one (6.25 MHz). The «УК-НЦ» group in the machine selection window. Numbers in the configurations and scripts are octal.

## Configurations

| Configuration | Contents | Description |
|---|---|---|
| УК-НЦ (HDD, AY) | K1515KhM2-001 gate array (БМК); slot 1 &ndash; IDE hard disk board, slot 2 &ndash; Aberrant Sound Module (3xAY-3-8912, DAC) | [UKNC.en.md](../../deploy/computers/uknc/UKNC.en.md) |
| УК-НЦ (БМК К1515ХМ1-031) | The same with the early gate array: the PPU works with memory more slowly | [UKNC-031.en.md](../../deploy/computers/uknc/UKNC-031.en.md) |
| УК-НЦ (Бейсик в ПЗУ) | Cartridge BASIC in slot 1, no hard disk and no sound module | [UKNC-basic.en.md](../../deploy/computers/uknc/UKNC-basic.en.md) |

Debug configurations: channel, display, hard disk, memory and reset tests.

In all configurations: two floppy drives, a tape recorder, a Covox or a printer on the parallel port 177100 (chosen on the toolbar), a mouse (177400 of the PPU), the S2 interface and the «UKNC Micronet» network adapter (going out to a serial port of the computer), the MS7007 on-screen keyboard.

## What is needed

The ROMs of the machine and of the cartridges come with the emulator: `deploy/computers/uknc/files/`. The floppies are in `software/uknc`: `sysimage.dsk` (FODOS, the factory test), `sys1002.dsk` (RT-11). The disk BASIC `basic.dsk` is in the software library on the emulator's site (https://ecat.emuverse.ru/library/uknc/).

Additionally, if needed:
* A hard disk image &ndash; does not come with the emulator; attach it with the hard disk button or put it at `software/uknc/hdd.img`.
* Booting over the S2 interface (HX protocol) &ndash; the [OpenHX](https://github.com/anpp/openhxserver) server and a pair of virtual ports (Windows &ndash; com0com, Linux &ndash; socat).
* Converting a WAV cassette recording to `.uknc.tap` &ndash; Python 3 and `wav2uknc.py` from the software library on the site.

## Files

| Medium | Format |
|---|---|
| Floppies | `.dsk`, `.hfe` |
| Hard disk | IDE image, CHS or LBA addressing |
| Tape recorder | `.uknc.tap` |
| ROM cartridges | `ide_wdromv0110.bin`, `ide_hdbootv0400.bin`, `ide-doubleRT5.bin`, `romctr_basic.bin`; chosen in the extension editor |

## Keyboard

НР, УПР, ГРАФ, ФИКС &ndash; Shift, Ctrl, Alt, Caps Lock; АР2 &ndash; Esc; К1&ndash;К5 &ndash; F1&ndash;F5; АЛФ (alphabet) &ndash; F6; ПОМ, ИСП, УСТ (help, execute, set-up) &ndash; F7&ndash;F9; СБРОС (screen clear) &ndash; F10; СТОП (HALT) &ndash; F12.
