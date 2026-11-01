# Elektronika MS 0585 with a hard disk

Elektronika MS 0585 with the hard disk controller in the first bus slot and a 10 MB
drive: 4 heads, 306 cylinders, 16 sectors of 512 bytes. Everything else is as in the
[main machine](MS0585.en.md).

The image slot is empty: the machine changes the 10 MB file on every boot, so use a
copy of it. Without an image the drive is not ready, and the self test shows the
picture of the system unit with device code 000401 and error code 000005.

## PROS 2.03

A ready system with games is in `software/e85/pros-2.03-10m.rd`. Put a copy of this
file into the hard disk slot and restart the machine: the main menu appears in 40
seconds. Drive 0 must be empty: with a floppy in it, the ROM boots from the floppy.

## Games

The menus of PROS are in Russian, so the names are given as they appear on the screen.

| Menu | Programs |
|---|---|
| Main | Посадка на Луну (Moon Landing), Лабиринт (Maze), Xonix, Сапер (Sapper), Карлики (Dwarfs) |
| А | Червячок (Worm), Backgammon, China-Town, Space Shuttle, Blackjack Trainer |
| Б | Пещера (Lode Runner), Вертолет (Helicopter), Каменщик (Bricklayer), Тетрис (Tetris) |

Menus А and Б open from «Дополнительные меню». Choose with the arrows, start with Do,
Help gives help. Most games are played on the numeric keypad rather than the arrows,
and Num Lock on the PC must be on. On a keyboard without a keypad, choose Settings >
Keyboard > «Numeric keypad on arrows» or «on letters» in the window menu, for the game
only: the menus of PROS want real arrows.

| Game | Start | Controls |
|---|---|---|
| Посадка на Луну | Resume | left and right arrows turn, up and down set the thrust |
| Лабиринт | at once | keypad: 8 up, 2 or 5 down, 4 left, 6 right |
| Xonix | Ctrl-O | arrows; Ctrl-C exits |
| Сапер | at once | keypad: 8, 2, 4, 6 |
| Карлики | any key | keypad: 4, 6, 8, 2 |
| Червячок | at once | keypad: 8 up, 2 or 5 down, 4 left, 6 right. Other digits ring the bell, **any other key, an arrow too, ends the game** |
| Backgammon | questions on the screen | typed text, Return |
| China-Town | P, while E exits | keypad: 4, 6; space |
| Space Shuttle | by itself | did not react to the keys tried |
| Blackjack Trainer | a menu | arrows, Do |
| Пещера | any key twice, then space on PLAY | keypad: 8 up, 2 or 5 down, 4 left, 6 right; comma and period set the speed, PF1 pauses, Ctrl-A gives up, Ctrl-Z exits, other keys stop |
| Вертолет | space twice | arrows. Tap them rather than hold: after 0.4 s the auto-repeat upsets the game |
| Каменщик | space | arrows |
| Тетрис | Resume twice | keypad: 7 left, 9 right, 8 rotates; space drops; Help shows the instructions, Exit ends |

Other games are installed from the «Обслуживание диска/дискетов» menu, the item
«Установить прикладную программу на диск». A menu holds 5 programs. Install from
`igra.dsk` and `igra-color.dsk` in separate sessions, with a reboot between them: the
system keeps the floppy it installed from in use.

## Installing from floppies

You can build the image yourself. A blank disk is any file shorter than 10 MB, at least
one 512-byte sector: the machine reads zeros past its end and extends it on writing.
Put it into the slot, and `pros-2.03-1-system.dsk` into drive 0. When the installer
asks, change the floppy in drive 2 (`pros-2.03-2-regim.dsk`, `-3-program`, `-4-bibliot`,
`-5-utilit`). Eject the floppy first, insert the next one, and only then press Resume.

Installing the system takes about 18 minutes of emulated time, up to 12 of them for
formatting and checking the disk. At the highest speed that is less than a minute.

## Not done

- The service zone of a sector is not stored; the controller only counts such writes.
- A 32 MB disk (8 heads, 511 cylinders) is set by the device parameters. The installer
  recognises and formats it, but the installation was not run to the end.
- A 5 MB disk (153 cylinders) is set the same way and was not tried.
