# Pravetz-8A with all cards

A Pravetz-8A fitted with every expansion card the emulator has.
It boots like the ordinary one, from the floppy drive in slot 6;
the other cards are started with the `PR#` command and the slot number.

| Where | Card | Start |
|---|---|---|
| X0 | MEGARAM 1 MB | &ndash; |
| Slot 1 | Printer card | `PR#1` |
| Slot 2 | Printer card for ПАРИС | `PR#2` |
| Slot 4 | U-LAN network card | `PR#4` |
| Slot 5 | Liberty Drive controller | `PR#5` |
| Slot 6 | KNGMD (floppy controller) 140 KB | at power-on |
| Slot 7 | COMport | `PR#7` |

## MEGARAM 1 MB

The X0 connector holds a 1 MB MEGARAM module: 16 pages of 64 KB.
A page is selected by writing its number to $C073 (as on the RamWorks), the video always shows the first one.
This is how the memory is used by the virtual disk of СП «Вариант» (the Variant joint venture), the `VIRD` program on the school disk `p8ru02.dsk`
(in `software/pravetz`): `BRUN VIRD` divides 1 MB into six disks, the drives `D1`&ndash;`D6` of slot 3 (`CATALOG,S3,D4`).
The disks are already formatted; F2 pressed at start formats them again, and F1 turns on a speaker click on every access.

## Printer cards

Both are parallel cards, copies of the Epson APL Printer Interface (such cards were built in the USSR; in Bulgaria this is the APC-01).
Printing: `PR#1` (slot 1, as the Pravetz-8A book says) or `PR#2`; back to the screen: `PR#0`.
The bytes the printer has received are shown in the «I/O ports» window, from which they can be saved to a file.
The ПАРИС publishing system from the Liberty disk prints to slot 2 (F1+P, the MULTIPRO T printer).

The extension editor selects the firmware of both cards:
* Epson APL Printer Interface;
* the firmware of the Soviet copy (the same circuit, slightly different firmware);
* a card of unknown origin from agatcomp.ru (the ROM says «THIS ROM IS JUNK, DO NOT COPY IT!»):
  built like the Epson APL, but it outputs the byte inverted, waits for the printer's answer after the strobe and recodes the characters itself.  
  The inversion is turned on together with this firmware.

## U-LAN network card

A joint development of СП «Вариант» and the Pravetz combine: an MC6850 serial port on a line shared by the whole classroom, 115 kbit/s.
The pupils' machines booted from the teacher's one over the network. `PR#4` starts the firmware: it prints the version and the station number and checks the line.
There is no schematic of the card, and the part that answers this check with an interrupt is not emulated:
the self test ends with the message «ДЕФЕКТ #1» (defect #1) and a return to BASIC.
The line can be taken out to a serial port of the PC (the `connect` command of the `ulan` device).
The extension editor selects the firmware: 2.1 (the SM7732 ROM) or 2.0.

## Liberty Drive

A non-standard floppy controller on the KR1818VG93 (Volna in Moscow; later СП «Вариант» in Tashkent):
5.25&prime; drives of 400 KB (40 tracks, 2 sides, 10 sectors of 512 bytes) under ProDOS.
The ПАРИС publishing system and the XONEX shells were distributed on such disks.

Booting: `PR#5`. The first Liberty drive holds the disk that came with the controller:
ProDOS, the XONEX SHELL (the date at start, arrows and Return), the file manager `/LIBERTY/LIBERTY.SYSTEM`
(commands: F1 and a letter, F1+F formats) and ПАРИС.
The extension editor selects the firmware of the card: Slim3.05 (Moscow) or Slim3.06 (Tashkent).
The card was reconstructed from the code of its ROM; there is no schematic. Formatting disks (writing a track) works.

## COMport

A card of СП «Вариант» (1996): an 8250 serial port through which the Pravetz worked with disks kept on an IBM PC.
The COMPORT.EXE program on the PC held 140 KB disk images, and to DOS 3.3 on the Pravetz the card was an ordinary floppy drive:
`D1` is the first image, `D2` the second, and so on. The emulator itself plays the PC here.

The card is in slot 7 because slot 5 is taken by the Liberty Drive. The real machine has the RGB card in slot 7,
but in the emulator colour is only a choice of monitor, and the COMport ROM finds its registers by the slot number.
Booting: `PR#7`: the card's ROM reads DOS from image 1 and installs the driver.
Image 1 must hold a DOS prepared for the card (like disk 1 of the COMPORT package);
the other numbers can take any 140 KB images in DOS order (images D1&ndash;D9 are selected in the extension editor,
or with the command `comport-pc.load(2, "game.dsk")`). What is written stays in the emulator's memory and in the saved state,
and goes to a file with the `save` command. The COPY copier from disk 1 moves programs between the drive in slot 6
and the images (in SET SLOTS the card's slot is 7).

The card and the protocol were reconstructed from the card's ROM, its schematic and the sources from agatcomp.ru.

For information on the computer, see the description of the base configuration.
