# Pravetz-82

The first mass-produced model (1982&ndash;1986, IMKO-2): a counterpart of the Apple ][+ with a modified ROM and its own character generator.
48 KB on the main board, 12 KB of ROM, a 140 KB floppy controller in slot 6.
Slot 0 holds the 128 KB RAM card KR3.088.184 (the first machines had a 16 KB RAM expansion card there).

## Differences from the Apple ][+

* The 128 KB card (a counterpart of the Legend Soft Disk 128 for the Apple ][) is eight 16 KB language cards on one board.
  A bank is selected by an access to $C084&ndash;$C087 (banks 0&ndash;3) and $C08C&ndash;$C08F (4&ndash;7),
  the rest works as on a language card; after power-on bank 0 is selected, and programs for 16 KB work as before.
  There is no schematic of the card: the registers are taken from the Saturn 128K card, which such cards are compatible with.
* Character generator: Cyrillic (capitals only) in place of the lowercase Latin letters, the letters sit at the bottom of the character cell.
  Ю, Ш, Щ and Ч are on the codes @, [, ] and ^, so texts of the 82 show these characters on the Pravetz-8A.
* ЯВЕРТЪ keyboard (the Bulgarian phonetic layout): the white and the yellow ЛАТ (Latin) keys (in place of the Apple's Shift and Caps Lock) switch between Latin and Cyrillic,
  МК is Control, ОСВ is Esc.
* The character generator dump from agatcomp.ru, according to its authors, was read with errors in a few characters.

## Keyboard

* ЛАТ (Latin/Cyrillic): F10.
* МК: Ctrl, ОСВ: Esc, RESET: F12 (only together with Ctrl), RPT: F11.

On the on-screen keyboard the white ЛАТ keys are Shift, the yellow ЛАТ switches the alphabet, and the lamp next to it is lit in Cyrillic.
The keyboard is selected in the extension editor. The early one (the default) has only the down arrow
and has RPT: there is no auto-repeat, as on the Apple ][+; a held key repeats (about 10 times a second)
while RPT is pressed: F11 or RPT on the on-screen keyboard.
The late one has the arrows and auto-repeat, without RPT.

## Tape recorder

The tape recorder works as on the Apple ][: `адрес.адресW` and `адрес.адресR` in the Monitor (адрес is an address), `SAVE` and `LOAD` in BASIC.
Tapes are `.ptp` files (records one after another), `.wav` (a digitized cassette) and `.bin` (one record for the Monitor's `R`);
what is recorded is saved to `.ptp`. The machine does not control the motor: start the tape with the button of the tape recorder window once the command is typed.

## Floppy drive

The extension editor selects the boot ROM of the KNGMD (floppy controller): for 16 sectors (DOS 3.3) or for 13 (DOS 3.2, `.d13` images).
The floppy drive is as on the Pravetz-8A, and it holds the same school disk СПТУ-20 with a menu.
All the disks in `software/pravetz` run on the Pravetz-82: the school disks `p8ru01.dsk` and `p8ru02.dsk` load DOS (the programs on them: `CATALOG`, `RUN`).
«Сокобан» (Sokoban) and «Арканоид» (Arkanoid) from the software library on the emulator's site (https://ecat.emuverse.ru/library/pravetz/) start straight away.
