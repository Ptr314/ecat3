# UKNC (Elektronika MS-0511)

An educational computer with **two** KM1801VM2 processors: the central one (8 MHz) and
the peripheral one (6.25 MHz).

This is the variant with a hard disk board and a sound module in the expansion slots.

## Keyboard

| Host | Machine |
|---|---|
| Shift, Ctrl, Alt, Caps Lock | НР, УПР, ГРАФ, ФИКС |
| Esc | АР2 |
| F1&ndash;F5 | К1&ndash;К5 |
| F6 | АЛФ (alphabet) &mdash; while it is held, the other alphabet is active |
| F7, F8, F9 | ПОМ, ИСП, УСТ (help, execute, set-up) |
| F10 | СБРОС &mdash; clears the screen |
| F12 | СТОП &mdash; the HALT signal of the CPU |

## Booting from floppies

The two drives hold floppies with system and application programs.

The boot drive can be chosen in the menu.

| Drive | Floppy | What is on it |
|---|---|---|
| 0 | `sysimage.dsk` | FODOS with system utilities: MACRO, LINK, LIBR, SRCCOM, BINCOM, DUMP, BUP, the EDIK editor, fonts. `RU TEST` &mdash; the factory «Проверка функционирования УК НЦ» (UKNC functional test) |
| 1 | `sys1002.dsk` | RT-11: DIR, PIP, DUP, NED, HELP, Pascal, floppy formatting (`RU MZTEST`) |

The floppy `basic.dsk` with the disk BASIC does not come with the emulator: it is in the software library on the emulator's site (https://ecat.emuverse.ru/library/uknc/), and it can be put into a drive from the emulator's menu.

__Note:__ KBS and ALTNUM have been removed from STARTS.COM on `sysimage.dsk`.
Both programs leave a resident part in the memory of the peripheral processor,
which breaks the factory test. They can be started by hand: `RU KBS`, `RU ALTNUM`.

## ROM cartridges

The first slot holds the hard disk board, the second one the sound module.

The ROM image of either slot is set in the extension editor (the «ROM image» field of
`cart1` and `cart2`). An empty socket is not occupied by anything. To boot from a
cartridge, choose item **2 - кассета ПЗУ** (ROM cartridge) in the ЗАГРУЗКА (boot) menu,
move to the slot number `(1,2)` with the right arrow, type it and press **ВК** (Enter).

The cartridge images lie next to the configuration:

| File | What it is |
| --- | --- |
| `ide_wdromv0110.bin` | hard disk loader WDROM V01.10, installed in the first slot |
| `ide_hdbootv0400.bin` | hard disk loader HDBOOT V04.00 with the IDDRIV V02.05 driver |
| `ide-doubleRT5.bin` | hard disk loader for RT-11 with two drives |
| `romctr_basic.bin` | cartridge BASIC |

## Hard disk

The first slot holds the IDE board with the ROM `ide_wdromv0110.bin` (WDROM V01.10).
Another loader is chosen in the extension editor (see «ROM cartridges»).

The hard disk can work in CHS or LBA addressing mode (chosen by the driver in the image itself).

No disk image comes with the emulator: attach one by hand before booting,
or put it at `software/uknc/hdd.img`.

## Tape recorder

BASIC programs are written to tape with the command `SAVE "ИМЯ"` and read with the command
`LOAD "ИМЯ"`, as on the real machine. Before `SAVE`, switch recording on in the tape recorder
window; type `LOAD` before pressing the play button. The recording is saved as a _*.uknc.tap_ file.
Programs in machine code are loaded with item **5** of the
ЗАГРУЗКА menu, **ВК**, and then the play button.

A recording of a real cassette in WAV is converted to _*.uknc.tap_ by the program
`wav2uknc.py` from the software library on the emulator's site (Python 3 is needed): `python wav2uknc.py запись.wav`.

## Sound and Covox

The one-bit speaker works through port 177716.
A **Covox**, an eight-bit DAC, is also installed on the parallel port 177100.
It can be turned off with a drop-down list on the toolbar. The same list puts a **printer**
on the connector instead of the Covox: what it receives is shown in the «Emulation / I/O ports» window,
and it is saved to a file from there too.

The second cartridge slot holds the **sound module**
[Aberrant Sound Module](https://github.com/aberranthacker/aberrant_sound_module):
three AY-3-8912 (177360, 177362, 177364) at 1.79 MHz and a DAC of its own (177372).

## Mouse

The mouse is always on, but a click in the window captures the pointer only once a program
has read the mouse; the middle button or Ctrl+Alt releases it, and so does a reset of the
machine. The button with a mouse on the toolbar turns it off completely: then
177400 does not answer, as on a machine without a mouse.

## S2 interface

The S2 interface (176570-176576) is a serial port through which the UKNC
boots RT-11 from disks on a PC over the HX protocol: item **4** of the ЗАГРУЗКА menu.

The line goes out to a serial port of the host. To connect an
[OpenHX](https://github.com/anpp/openhxserver) server on the same computer, you need
a pair of virtual ports:

* Windows &ndash; [com0com](https://com0com.sourceforge.net/), for example the pair
  COM5&harr;COM6;
* Linux &ndash; `socat -d -d pty,raw,echo=0 pty,raw,echo=0`, which prints two
  paths like `/dev/pts/3` and `/dev/pts/4`.

One end of the pair goes to OpenHX (speed 9600, an RT-11 image with the HX.SYS driver
in slot HX0), the other &ndash; to the emulator: the `port = COM5` parameter of
the `c2` device in the configuration. Then item **4** of the ЗАГРУЗКА menu and **ВК**.

## Network adapter

The network adapter SA (176560-176566) is the adapter of the KUVT (classroom computer
set) network «UKNC Micronet». The station number is set in the extension editor.
Like that of the S2, its line goes out to a serial port of the host.

## Other

More about the computer:

* [Wikipedia article](https://ru.wikipedia.org/wiki/Электроника_МС_0511)

Technical documentation and software:

* [On Emuverse.ru](https://emuverse.ru/wiki/УКНЦ)
