# Pravetz-8A

A Bulgarian personal computer of the Pravetz combine (1986&ndash;1994), also made in Tashkent by the joint venture «Вариант» (Variant) for schools of the USSR.
It is compatible with the Apple //e in circuitry and software, but it is not a full clone.
The emulated variant is the one for the USSR: 64 KB on the main board, a 64 KB MEGARAM module in the X0 connector, a 140 KB floppy controller in slot 6.

How the Pravetz-8A differs from the Apple //e:

* The SM630 processor is an NMOS 6502 (not a 65C02), with a 14250.45 kHz crystal, as on the European //e: 50 frames a second, 312 lines.
* Its own 16 KB ROM: a rewritten Monitor with a mini-assembler and a disassembler, a BASIC with hexadecimal numbers (`PRINT $20+$B`),
  the commands `SETMOD`, `LINE INPUT`, `CONV` and formatted output.
  There is no tape recorder, nor the LOAD/SAVE/SHLOAD tape commands.
* Two code tables. MODE0 (7-bit, as on the Pravetz-82): capitals only, Cyrillic on the codes $E0&ndash;$FE.
  MODE1 (8-bit, `SETMOD 1`): capital and lowercase Latin and Cyrillic, the alternative character set.
  The table is selected by a write to $C060 (bit 0); on a read, bit 6 is the inverse of what was written, and the eighth bit of the key code is read in bit 7.
* A character generator with Cyrillic instead of MouseText and flashing characters; the ROM holds two sets, selected by the jumper X20.
* A monochrome monitor (the green VMM 3107). Colour only through the RGB card in slot 7.
* MEGARAM up to 1 MB in 64 KB pages, the page number is written to $C073.

## Using the computer

At power-on the ROM prints «Правец» and boots from the floppy. The drive holds the school disk СПТУ-20 (from a vocational school in the town of Lomonosov):
DOS 3.3 and a menu, a program is chosen with a letter. Tetris (TETA.OBJ) does not start from the menu, on the real machine either:
first `BLOAD RUNTIME`, then `BRUN TETA.OBJ`. CTRL+RESET (Ctrl+F12) exits to BASIC. `PR#3` gives 80 columns,
`SETMOD 1` lowercase letters. CTRL+RESET+F2 enters the Monitor, CTRL+RESET+F1 is a cold restart.

## Keyboard

* C/L Lock (Latin/Cyrillic): F10. Cyrillic by ГОСТ (the Soviet standard, ЙЦУКЕН).
* Caps Lock: Caps Lock (capitals in the 8-bit table).
* CTRL: Ctrl, RESET: F12 (only together with Ctrl).
* F1, F2: F1, F2 (wired to the joystick buttons, $C061/$C062).
* DEL: Delete; the arrows are the arrows.

The integrated package ПАРИС (the Bulgarian one of 1987 and the Russian one of 1990) recognises the Pravetz at start and works in the 8-bit table:
C/L Lock switches the alphabet. Its letter case is reversed: a key without Shift gives a capital, with Shift a lowercase letter;
with Caps Lock on it is the other way round, the usual way. It turns on its fallback mode, with the alphabet on CTRL+A, only on a real Apple //e.

The on-screen keyboard (the «Keyboard» item of the «Emulation» menu) is the one of the selected layout: for the USSR (ГОСТ) a PC-style keyboard,
the Bulgarian ones (БДС, the Bulgarian standard, and ЯВЕРТЪ, the phonetic one) a 65-key reed-switch keyboard with stepped rows. The lamps show Caps Lock and C/L Lock.

## Variants

The extension editor selects the boot ROM of the KNGMD (floppy controller) (16 sectors, or 13 for DOS 3.2 disks, `.d13`),
the character generator firmware (for the USSR, Bulgarian, СП «Вариант» (the Variant joint venture): in the «Вариант» one Ы and Ъ are swapped, the Bulgarian one has an empty rectangle and a checkerboard in place of Ы),
the jumper X20 (the Pravetz set or the Apple //e set) and the Cyrillic layout with its keyboard (ГОСТ, БДС, ЯВЕРТЪ).

## Display

The monitor is selected in the «Video output» list on the toolbar: green (the standard one), white, amber, an RGB card with the ideal Apple colours,
the RGB card of СП «Вариант» (the colours from its ROM on a CGA monitor) or the standard Bulgarian RGB card, which gave the "wrong" colours.

## File support

140 KB floppy drive: DSK, DO, PO, NIB, NIC, MFM, HFE images.

The `software/pravetz` directory holds disks from agatcomp.ru: the school disk СПТУ-20 (`sptu20.dsk`) and two school disks
with games and programs in BASIC (`p8ru01.dsk`, `p8ru02.dsk`).
The software library on the emulator's site (https://ecat.emuverse.ru/library/pravetz/) has
«Сокобан» (Sokoban) (`sokoban.dsk`; works only on the NMOS processor SM630, not on a 65C02),
«Арканоид» (Arkanoid) (`arkanoid.dsk`) and «Игра и математика» (Game and Mathematics) (`igra-i-matematika.nib`, ДИ «Народна просвета» (the state publisher Narodna Prosveta), 1986:
an image of the tracks as they are, with their own loader; it works only if the disk is write-protected).

The floppy controller works like the real card: the sequencer, driven by its ROM, reads and writes the disk bit by bit,
with real timing, so copy-protected disks and formatting (`INIT`) work.

* Programs and a description: on the [agatcomp.ru](https://agatcomp.ru/Pravetz/index.shtml) server
* Book: P. Petrov, «Персональный компьютер Правец-8А» (The Pravetz-8A Personal Computer), Sofia, «Техника» (Tekhnika), 1989.
