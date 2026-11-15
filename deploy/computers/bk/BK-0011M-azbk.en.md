# BK-0011M + AZBK

A BK-0011M with the AZBK controller (MAXIOL) in the MPI bus connector: 32 MB of memory
in 4 KB windows, a 1024x768 VGA output with three layers and a palette of 32768 colours,
a blitter, a Covox, a sound DMA (PCM and IMA ADPCM, 44100 Hz) and a stereo pair of AY
chips, the AZ disk
controller working on images, non-volatile memory and a clock. The controller firmware
is MAXIOL's package 00019.

The machine is a stock one, without modifications: the controller does not switch the
BK's memory off but repeats its writes in its own memory (the shadow), and shows the
ordinary BK screen from there as well.

## Start-up

At power-on the controller's start ROM beeps and restarts the board together with the
BK, then shows the BIOS screen: the checks, the processor frequency, the network (there
is none: the address is zero, and twenty attempts to get the time take about 15 seconds)
&ndash; and boots the system from the pseudo-disk AZ0. At start-up КТ runs the SETUP
program, Space goes to the BK monitor, digits choose the disk.

## Pseudo-disks

Four pseudo-disks AZ0&ndash;AZ3 &ndash; images of 512-byte blocks (*.img, *.bkd, *.dsk
of any size) &ndash; are attached with the buttons on the toolbar. AZ0 holds ANDOS 3.30;
writes to the image that comes with the emulator go to memory only. The system boots
from AZ0 through the controller's AZ337 ROM. The board has no floppy drive: a БК floppy
image (*.bkd) is attached the same way, as a pseudo-disk, and AZ337 boots the system
from it as the floppy controller ROM would from a diskette.

## Screen

The picture is 1024x768: modes of 2, 4, 16 and 256 colours, with three layers or
without, with repeated dots and lines and scrolling. The ordinary BK screen is the
512x256 and 256x256 modes over the shadow of pages 5 and 6, with the palettes of
register 0177662.

## microSD card

The card is a folder on the computer. At start-up the socket is empty; the SD button on
the toolbar chooses the folder, takes the card out and turns writing into the folder on,
and the choice is remembered for the machine, like disk images. The folder can also be set
in the extension editor, in a copy of the machine. The controller sees
the files and folders there whose names are 8.3 (NAME.EXT, upper and lower case alike)
and nothing else, and a program cannot get outside the folder. If the card has an AZ.INI, at power-on
and when the board restarts AZ0&ndash;AZ3 take the disks of its [DISKS] section, as on the
real board (an image on a drive AZ.INI does not name stays). MAXIOL's AZMNT (in the
software library), AZUMNT and AZSMNT work (mounting an image from the card on
AZ0&ndash;AZ3), as do reading and writing
files (SETUP writes AZ.INI), loading a file into the controller's memory and saving a
screenshot to a file (into SCREENS, if the folder has one). What the machine writes stays
in the card's memory and in a saved state by default, and the folder is left as it was; to
write into the folder itself, use "Write into the folder" of the SD button (turning it on
offers to write what is already in memory into the folder too).

## Not done

* The clock of the controller's pair of AY chips (ports 0177172&ndash;0177175) is not
  described and is taken as 12 MHz / 7, as on the BK sound cards. The chip type, YM2149
  or AY-3-8910, is chosen in SETUP, as on the board. The BK's AY sound card on port
  0177714 works as usual.
* The network, the Hall of Fame, the UART, a PS/2 keyboard.
* The access time of the controller's memory is not described and is taken as zero.

## Joystick and mouse

One of three devices is plugged into the УП connector (the user port, port 0177714),
chosen from a list on the toolbar. After start-up the connector is empty, and the
arrows work only as keys.

* Standard BK joystick: arrows &ndash; directions, End &ndash; button A,
  Home &ndash; fire, PgUp &ndash; extra fire, PgDn &ndash; button B.
  The arrow keys are then pressed on the machine's keyboard as well.
* UVK-01 "Marsianka" mouse: a click on the screen hands the computer's mouse over to the
  machine, and the cursor disappears. The left button is button «Л», the right one is «П».
  Ctrl-Alt, the middle button or switching to another window gives the mouse back, and in
  a browser Esc does too. The speed is set in the «Settings» &rarr; «Mouse speed» menu, in a
  browser &ndash; with the list next to the input device selector.
* Printer: the output is in the «Emulation / I/O ports» window, and it can be saved to a
  file from there too. Better switch the AY sound card off while printing, since it uses
  the same ports.

## Keyboard

* ВВОД: Enter, ТАБ: Tab, ЗАБ: Backspace, СБР (clear): Home, arrows: arrows.
* Нижний регистр (lower register): Shift, СУ: Ctrl, АР2 (alternate register 2): Alt.
* РУС/ЛАТ (Russian/Latin): Caps Lock. With Caps Lock on, Russian letters are typed by
  their places on the PC keyboard (ЙЦУКЕН). РУС and ЛАТ on their own are Ctrl-N and
  Ctrl-O.
* Letter case is chosen by the ЗАГЛ (capitals) and СТР (lower case) latches, as on the
  on-screen keyboard: after start-up letters are capitals in both ЛАТ and РУС.
* Letter, digit and symbol keys match a standard QWERTY keyboard with the English layout.
* Ctrl-Break &ndash; reset, Ctrl-Alt-Break &ndash; cold restart.
* The on-screen keyboard is available.

## Programs and tape recorder

Programs in the *.bin format (with a load address header) are opened from the
«File» menu. The tape recorder both reads and writes.

* Loading: open a *.bin, *.cod, *.foc or *.asc file in the tape recorder window,
  start reading from tape on the machine and press Play.
* A file with another extension, for example *.ovl, goes onto the tape under its full
  name. This is how a program that loads its own parts finds it: when the program asks
  for the file, open it in the tape recorder window and press Play.
* Saving: turn on recording in the tape recorder window, start output to tape on the
  machine, then stop recording &mdash; the emulator offers to save what was recorded,
  with a name and extension suggested by what ended up on the tape.
* A BASIC program saved with the `SAVE"ИМЯ"` command is written as text
  and comes off the tape as a *.asc file. Such a file is read back with `LOAD"ИМЯ"`.
