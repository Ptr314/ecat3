# Poisk-1

An IBM PC compatible computer by the Elektronmash production association (Kyiv, 1989):
* The K1810VM88 processor (an Intel 8088 counterpart), 5 MHz.
* 512 KB of RAM on the board (the version with 128 KB is a separate configuration).
* A video system on a gate array (BMK): 320x200 in four colours or 640x200, no text
  mode.
* The B504 floppy cartridge: KR1818VG93, two 5.25&quot; 720 KB drives
  (80 tracks, 2 sides, 9 sectors), images are sector dumps
  (*.img, *.ima, *.dsk).
* A VI53 timer, a VN59 interrupt controller, two VV55: keyboard, video
  mode, speaker, tape recorder.
* Tape recorder: the IBM PC cassette format, *.cas files (the format of the PCE emulator),
  reading and writing.

The ROM is the factory BIOS of 14.02.1991 (`poisk_1991.bin`, 8 KB),
the ROM of the floppy cartridge is the factory one (`b_ngmd_n.rf2`).
Without a floppy in A: the BIOS opens its menu after about 15 seconds:
F1 &mdash; boot from cassette, F2 &mdash; from ROM.

Before every operation the floppy driver waits for the motor to spin up (about a quarter
of a second), so MS-DOS takes about a minute to boot &mdash; the same as
on a real machine with this ROM.

## Tape recorder

Loading from cassette is done from the BIOS menu: F1, the file name,
Enter, then start the tape in the tape recorder window and press Enter again; when it finds
the file, the BIOS loads it and passes control to it after «Запуск (ВК)» (Start (Return)).
This BIOS does not control the tape recorder motor: the tape is started and
stopped with the buttons. The menu has no recording: programs write to the cassette through
INT 15h (function 03h), and what is recorded is saved as a *.cas file.

Many digitized cassettes are stored not as a recording but as the file itself, without a
header (for example, TETRIS.CAS). The tape recorder gives such a file a header
with the file name without the extension: that is what you type in the menu, minding
the case &mdash; for TETRIS.CAS it is TETRIS (capitals with
Caps Lock). The BIOS skips a file with another name found on the tape, reporting
«Найден файл: ...» (File found: ...), and searches on to the end of the tape. Reading goes at
the speed of a real cassette: the 19 KB of Tetris take about two minutes.

## Keyboard:

* Letter, digit and symbol keys &mdash; as on a standard
  PC keyboard with the English layout; Shift, Ctrl, Alt, Caps Lock, Num Lock,
  Scroll Lock, F1&ndash;F10 &mdash; in their places.
* Arrows, Home, End, PgUp, PgDn, Ins, Del &mdash; the keys of the Poisk numeric keypad
  with these meanings.
* Latin/Cyrillic switch (the right Ctrl of the Poisk): F12.
* Ukrainian І: F11. Ї, Ё, Є, Пауза (Pause) and the digit 5 of the numeric keypad are not mapped
  to the host keyboard.

## Other

More about the computer:

* [Wikipedia article (in Russian)](https://ru.wikipedia.org/wiki/Поиск_(компьютер))
