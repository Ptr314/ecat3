# DVK-4 MS0507.04 (MS1201.04, KTsGD, MX, MY, DW)

A DVK (dialogue computing complex) on the MS1201.04 processor board
(KM1801VM3, 1 MB of RAM) with the KTsGD colour graphics display controller,
MY and MX floppy drives and a DW hard disk.

The KM1801VM3 is a processor of the PDP-11/34 class: kernel and user modes,
a memory management unit with 22-bit addresses. It runs RT-11XM.

## KTsGD

The KTsGD is a graphics terminal on a bus board: its own KM1801VM2
processor, 128 KB of video memory, an 800 x 480 screen in 16 colours, its own
MS7004 keyboard. It is connected to the machine by an IRPS line (a current loop serial interface).

The KTsGD firmware is a replaceable chip; 181 is installed by default, the others
are chosen in the configuration editor (a copy of the machine, the «Прошивка КЦГД» (KTsGD firmware) field):

* **181** &mdash; the default, for compatibility with games.
* **182** &mdash; an improved version, but some programs do not work with it.
* **182** with the KeyGP expansion ROM (RR1-183, RR1-184); it also causes compatibility problems.

The KTsGD mouse is always plugged in, but a click on the screen captures the pointer only
once a program has read the mouse; Ctrl+Alt releases it. The mouse button on the
toolbar can unplug it.

## Starting

The board's monitor is a replaceable chip: the factory 134 (the default) or 377
(NPP Kristall, 1991), which is chosen in the configuration editor: a copy of the
machine, the «ПЗУ монитора» (monitor ROM) field.

**377** tries to boot by itself in this order: MY0, MY1, DW.
Without an image in the hard disk the hard disk test reports an error, and the monitor stays in halt mode.
To boot by hand, type `B`, then at the `Boot:` prompt the device name and the drive
number: `MY0`, `DW0`. `T0`-`T6` run the tests.

## Booting from a floppy

Drive 0 of MY holds RT-11SJ V05.04. The monitor command

`@BMY0`

boots the system (the device name and the drive number, with no spaces and no ВК (carriage return)).
In the same way `BMX0` boots from MX.

The same floppy holds the RT-11XM monitor with the MYX and MXX drivers: it works with
the memory management unit and sees the whole megabyte. From RT-11SJ

`.BOOT MY0:RT11XM`

reboots the system into XM.

## DW hard disk

No image is inserted into the hard disk. RT-11 V05.04 on a hard disk (5 MB, the drivers
DW and DWX, the tests TESTDW and TDW, games, `rt11-dw.dsk`) does not come with the
emulator; it is in the software library on the emulator's site (https://ecat.emuverse.ru/library/dvk/).
Insert it from the hard disk menu (in the web version, choose the file on your
computer) and boot it with

`@BDW0`

## Serial port

The processor board has its own IRPS port (KR1801VP1-065, connector XP1):
176570, vectors 370 and 374, 9600 baud. The host port for it is chosen in the configuration editor
(a copy of the machine, the `tl` device); what a program sends to the port is shown in the "Emulation / I/O ports" window.

## Parallel port

The parallel port of the board (KR1801VP1-033, connector XP2) serves as an LP11
printer port: 177514/177516, vector 200. The RT-11 LP driver works with it without any setup
(on the `rt11-dw.dsk` hard disk: `COPY файл LP:`, where файл is the file name). A printer is on the connector (the `lpt` device):
what it receives is shown in the "Emulation / I/O ports" window, and it is saved to a file
from there too.

## Keyboard

| Host | Machine |
|---|---|
| Shift, Ctrl, Caps Lock | ВР, СУ, ФКС |
| Alt | РУС/ЛАТ |
| Esc | Ф11 (АР2, ESC) |
| F1&ndash;F4 | ПФ1&ndash;ПФ4 (PF1&ndash;PF4) |
| F5&ndash;F9 | СТОП КАДР, ПЕЧАТЬ КАДРА, ПАУЗА, УСТ РЕЖИМА, Ф5 (Hold Screen, Print Screen, Pause, Set-Up, F5) |

Latin letters are on the keys with the same Latin label; in the Russian
case you get the ЙЦУКЕН layout of the MS7004 itself.
