# DVK-3 (MS1201.02, KSM, KGD, MX, MY, DW)

A DVK (dialogue computing complex) on the MS1201.02 processor board
(KM1801VM2) with the KSM terminal, the MS7004 keyboard, the KGD graphics
controller, MX and MY floppy drives and a DW hard disk.

The console monitor is a replaceable chip: 279 boots from MX, MY and DW, the earlier
055 &mdash; only from MX. 279 is installed by default; 055 is chosen in the configuration
editor &mdash; a copy of the machine, the «ПЗУ монитора» (monitor ROM) field.

The console switches are two latching buttons on the toolbar: "Halt"
(the processor is in halt mode while the button is pressed) and "Line clock 50 Hz"
(the interrupts switch: the 50 Hz line clock).

## Booting from a floppy

Drive 0 of MY holds RT-11SJ V05.04. The command of the 279 monitor

`@BMY`

boots the system. Drive 0 of MX holds RT-11SJ V05.04 with the MX V02.2 driver,
the command is `BMX`; to work with the system floppy after that, use `ASS MX0 DK`.

## DW hard disk

No image is inserted into the hard disk. RT-11 V05.04 on a hard disk (5 MB, `rt11-dw.dsk`)
does not come with the emulator; it is in the software library on the emulator's site
(https://ecat.emuverse.ru/library/dvk/). Insert it from the hard disk menu (in the web version,
choose the file on your computer) and boot it with the command

`@BDW`

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
