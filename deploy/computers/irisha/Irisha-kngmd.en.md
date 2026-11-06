# Irisha (KNGMD)

The Irisha-M version with the RTV*001.89 floppy controller module (KNGMD):
* Main memory: 64 KB on the video board.
* The KNGMD module: a KR1818VG93 controller, a ROM with BIOS version 4U and RAM of its own, which holds a RAM disk.
* 5.25&quot; drives A: and B: (two sides, 40 tracks, 9 sectors of 512 bytes). A: holds a floppy with a set of CP/M utilities.
* 8&quot; drives E: and F: on the GMD7012 controller (ports 50-51). E: holds a floppy with sample games.
* RAM disk D: in the memory of the module.
* ROM extension on port 14 with games and a text editor.
* Tape recorder input and output.

To boot CP/M, choose «Диск» (Disk) in the start menu. When asked whether to clear the RAM disk, answer Y (first start) or N, then press Return.

## Keyboard:

* РУС/ЛАТ (Russian/Latin): Caps Lock.
* ВК (carriage return): Enter, ПС (line feed): F12, ГТ (tab): Tab, АР2 (escape): Esc, arrows: arrows.
* СУ (control): Ctrl, Shift &mdash; upper-case characters.
* Letter, digit and symbol keys &mdash; as on a standard QWERTY keyboard with the English layout.
* Ctrl-Break &ndash; reset, Ctrl-Alt-Break &ndash; cold restart.
* The on-screen keyboard is available.

In the ИРИТЕКСТ (IRITEXT) editor, Ctrl-N / Ctrl-O switch the layout. To switch to ЙЦУКЕН (the Russian layout), use Caps Lock.

## Supported files

The 5.25&quot; drives take 360 KB images in two variants:
* .cpm &mdash; tracks in BIOS order: first all the tracks of the first side, then those of the second;
* .dsk &mdash; a dump with the sides interleaved by cylinder (images read from floppies and converted from IMD files look the same).

The order is chosen by the file extension. When an image is saved under the other extension, the tracks are reordered, so the drive can convert .dsk to .cpm and back.

The GMD7012 drive takes the .gmd format, 256 KB in size.

The tape recorder loads and saves files in the .bin format &mdash; plain data without headers or encoding.

## Other

More about the computer:

* [Wikipedia article (in Russian)](https://ru.wikipedia.org/wiki/Ириша)

Technical documentation:

* [On Emuverse.ru](https://emuverse.ru/wiki/Ириша)
* [The RTV*001.89 floppy controller module](https://www.emuverse.ru/wiki/Ириша/Модуль_контроллера_НГМД_RTV*001.89_ТО)
