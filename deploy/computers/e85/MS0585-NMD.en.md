# Elektronika MS 0585 with a hard disk

Elektronika MS 0585 with the hard disk controller in the first bus slot and a 10 MB
drive: 4 heads, 306 cylinders, 16 sectors of 512 bytes. Everything else is as in the
main machine.

The image slot is empty: the machine changes the 10 MB file on every boot, so use a
copy of it. Without a drive the controller firmware, as on the real machine, never
leaves its reset, and its registers do not answer: the self test polls the slot for
about 20 seconds with a black screen and shows the picture of the system unit with
code 177776 (slot occupied, registers do not answer).

## PROS 2.03

A ready system with games is the image `pros-2.03-10m.rd`. It does not come with the
emulator: it and the floppies below are in the [software library on the emulator's site](https://ecat.emuverse.ru/library/e85/).
Put a copy of this file into the hard disk slot and restart
the machine: the main menu appears in 40 seconds. Drive 0 must be empty: with a floppy
in it, the ROM boots from the floppy.

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

How to build a new disk image. A blank disk is any file shorter than 10 MB, at least
one 512-byte sector, and not of the size of one of the drives below: the machine reads
zeros past its end and extends it on writing.
Put it into the slot, and `pros-2.03-1-system.dsk` into drive 0. When the installer
asks, change the floppy in drive 2 (`pros-2.03-2-regim.dsk`, `-3-program`, `-4-bibliot`,
`-5-utilit`). A new image can be loaded straight over the old one: the drive shows the
machine a disk change and is not ready for a second and a half. Press Resume after that;
pressed earlier, it leaves the installer at the same prompt, and has to be pressed again.

Installing the system takes about 18 minutes of emulated time, up to 12 of them for
formatting and checking the disk. You can use the turbo mode of the emulation to avoid waiting.

## Images of other sizes and DEC systems

The controller tells the geometry of an image by the size of the file. It knows the DEC
Professional drives RD50 (5 MB), RD51 (10 MB), RD31 (21 MB), RD52 (36 MB), RD32 (43 MB) and RD53 (71 MB),
with 16 and with 17 sectors a track, as the Xhomer emulator does, two RD52 drives of other
makers (5 heads and 697 cylinders, 7 heads and 645 cylinders), which the DW handler of
RT-11 V5.4-V5.7 tells apart, and 32 MB of 8 heads and 511 cylinders, where PROS installs. A file of another size takes the geometry from the device
parameters: 4 heads and 306 cylinders, which can be changed in the extension editor. This is
how PROS 2.03 installs from the floppies on 5 and 32 MB; there are no ready images of those
sizes.

The library has two 32 MB images with systems for the DEC Professional:

- `rt11v53.rd`: RT-11 V5.3 with the XM monitor, the prompt comes after 40 seconds;
- `venix2.rd`: VENIX/PRO 2.0, the Unix that came with Xhomer. After a minute and a half,
  once the file systems are checked, `login:` appears. User `root` gets a shell, `demo` the
  colour demo; neither has a password. VENIX shows the year as 1988: it does not take years
  after 1999 from the clock.

DEC programs take a character by the place of the key on the LK201 keyboard, while the Latin
letters of MS7004 are laid out as ЙЦУКЕН, so for them choose the LK201 layout in the extension
editor.

## Not done

- The service zone of a sector is not stored; the controller only counts such writes.
- The speed of the disk is not the real one. The technical description gives no times, they are
  taken from the DVK hard disk, and the disk does not turn: a sector comes right after the seek. A
  real drive is slower, by how much is not known, so the time PROS takes to boot here is no measure.
