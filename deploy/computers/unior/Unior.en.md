# Unior FV-6506

A Kyiv machine of 1989&ndash;1991, a development of the Radio-86RK: a 2.2 MHz processor,
128 KB of RAM, an 80&times;25 colour screen, lowercase letters, 240&times;128
pseudographics, sound on a KR580VI53 and an 88-key keyboard.

The standard system is TCP/M-80M: a CP/M with a tape recorder instead of a floppy drive.
The tape is divided into sectors and is read like a floppy, and the machine winds the
tape by itself.

The machine has the full expansion: seven blocks of 64 KB on top of the main memory. The
monitor's <tt>L</tt> command loads from block 7, and the TCP/M RAM disk lives in block 1.

## Booting the system

The tape recorder already holds the cassette with the &laquo;Елкон&raquo; games
(<tt>elcon.a.bt</tt>), and the system is on it too. Give the monitor command <tt>I</tt>: it
reads the first record from the tape and runs it. This is the loader, and after it TCP/M
reads itself in. Another tape (<tt>.bt</tt>) can be inserted in the tape recorder window.

There is no need to press the buttons in the window: the machine drives the tape transport,
and the window only shows what is going on. The tape runs at the real speed, 2400 baud, so
the <tt>A&gt;</tt> prompt appears after about two and a half minutes, exactly as on a real
machine.

The system has three disks:

* <tt>A:</tt> &ndash; the RAM disk in the extra memory, where the loader has put the system.
  <tt>DIR</tt> without a letter shows this disk, and running a program from <tt>A:</tt>
  does not move the tape;
* <tt>B:</tt> &ndash; the cassette in the tape recorder. Its label is shown in the top line of
  the screen;
* <tt>C:</tt> &ndash; a second cassette volume: the system asks you to change the tape and
  waits for a space.

The top line also shows what the tape transport is doing: <tt>&gt;&gt;</tt>,
<tt>&lt;&lt;</tt> or <tt>WR</tt>.

Under TCP/M a write does not reach the tape at once: the system keeps the sector in memory
and writes it out to the cassette when it no longer needs it, on a restart or when another
sector is accessed.

## Writing to tape

The monitor command <tt>E</tt> writes a block of memory to tape and starts the tape
transport by itself.

The standard <tt>COPY</tt> of TCP/M does not touch the tape transport: <b>you start the
recording yourself</b>, with the &laquo;Record&raquo; button in the tape recorder window.

<pre>COPY CT:=A:CPM55     write the file to a blank cassette
COPY A:=CT:CPM55     read it back</pre>

A blank cassette is a <tt>.bt</tt> image made of a single four-byte header. Such a tape is
half an hour long, one side of a C-60.

What has been recorded stays in the tape recorder itself until the cassette is saved: the
window offers to do that when the tape is changed.

## Monitor commands

A single Latin letter from <tt>C</tt> to <tt>M</tt>:

| | |
|---|---|
| <tt>C</tt> start,end | checksum of a block |
| <tt>D</tt> start,end | memory dump |
| <tt>E</tt> start,end | writing a block to tape |
| <tt>F</tt> byte,start,end | filling memory |
| <tt>G</tt> address | viewing and editing memory |
| <tt>H</tt> | viewing and editing registers |
| <tt>I</tt> | input from the tape recorder and running what was read |
| <tt>J</tt> address | byte-by-byte viewing with editing |
| <tt>K</tt>, <tt>M</tt> | transfer between the main and the extra memory |
| <tt>L</tt> | loading 16 KB from block 7 and running it |

The same commands are on the additional key block: К1&ndash;К5 give A&ndash;E, and ПОМ, УСТ
and ИСП give G, H and I.

## Keyboard

There is an on-screen keyboard with all 88 keys, the numeric keypad and the function keys
included. A key of the picture closes the very same contact of the matrix as the real key,
so with it you can also press what has no key on the host.

The modifiers are part of the matrix itself, and their state is kept by the ROM; it is also
shown on the right of the top line of the screen. On the picture НР (Shift) and УПР (Ctrl)
latch until the next key, while АЛФ (alphabet), ГРАФ (graphics) and ФИКС (lock) toggle a
flag. СБРОС (reset) on the picture works only together with УПР, so that a stray click does
not throw your work away.

On the host:

* letters, digits and symbols are in their places. On the Unior itself the period, comma,
  slash, minus and the signs above the digits are typed together with НР (without НР the
  period key gives &laquo;&gt;&raquo;); the emulator presses and releases НР by itself, so
  type them as you would on the host;
* ВК: Enter, ТАБ: Tab, АР2: Esc, ЗБ: Backspace, arrows: arrows, СТОП: `~`;
* АЛФ: Caps Lock, ФИКС: Scroll Lock, ГРАФ: Alt, НР: Shift, УПР: Ctrl;
* К1&ndash;К5: F1&ndash;F5; ПОМ: Home, УСТ: Insert, ИСП: Delete;
* numeric keypad: the digits on F6&ndash;F12, PgUp, PgDn, End; `+`, `-` and `*` on the
  grey keys (the grey Enter gives the same `*`), `.` on NumLock. The comma of the numeric
  keypad is only on the on-screen keyboard.

## File support

The tape recorder works with <tt>.bt</tt> tape images: a container with the timings and the
data of the records, the same as on the Argo.

## Cassettes

Each of the TCP/M cassettes boots by itself: insert it in the tape recorder window and give
the <tt>I</tt> command. Next to each one there is a <tt>.lst</tt> with the list of its files.
The emulator comes with <tt>elcon.a.bt</tt>; the cassettes <tt>pasc.a.bt</tt> and
<tt>sc.a.bt</tt> are in the [software library on the emulator's site](https://ecat.emuverse.ru/library/unior/).

* <tt>elcon.a.bt</tt> &ndash; inserted from the very start: games by the &laquo;Елкон&raquo;
  and &laquo;Вільні майстри&raquo; groups with their descriptions: GNOMIK, AKAD, BUILD, PIPE,
  TRAIN, PITON, PYRAMID, PACKMAN, PIT2, KOMKON;
* <tt>pasc.a.bt</tt> &ndash; Pascal/MT+ 5.2: compiler, overlays, linker, libraries;
* <tt>sc.a.bt</tt> &ndash; the SuperCalc 1.12 spreadsheet.

Games are run from the cassette they are on: first <tt>B:</tt>, then the name of the program.
A game looks for its level files (<tt>.STG</tt>) on the current disk and will not find them
from <tt>A&gt;</tt>.

The images survived with damage, and these three cassettes have been restored: the corrupted
sectors were taken from other cassettes where the same files had survived.

## Links

* [Emuverse page](https://emuverse.ru/wiki/Юниор_ФВ-6506)
* [Wikipedia article](https://ru.wikipedia.org/wiki/Юниор_ФВ-6506)
* [Schematic, dumps, tapes and photos](https://alemorf.github.io/retro_computers/computer.html?id=Unior_FV-6506)
