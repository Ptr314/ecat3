# Argo FV-6511

The successor of the Unior FV-6506. There are three differences: the processor, other port
addresses, and the character generator, which is in RAM.

The main feature of the machine is <b>the ZX Spectrum mode</b>: a program started from a
cassette puts the Spectrum ROM into RAM and switches the screen to the Spectrum's raster.

## Booting the system

The system cassette is already in the tape recorder. Give the monitor command <tt>I</tt>:
it reads the first record from the tape and runs it, and after that TCP/M reads itself in.
Another tape (<tt>.bt</tt>) can be inserted in the tape recorder window.

There is no need to press the buttons in the window: the machine drives the tape transport.
The tape runs at the real speed, so the <tt>A&gt;</tt> prompt appears after about two and a
half minutes.

Disks: <tt>A:</tt> &ndash; the RAM disk in the extra memory, <tt>B:</tt> &ndash; the
cassette in the tape recorder (its label is shown in the top line of the screen).

<b>Ф9</b> (F9) takes you from CP/M to the monitor, and the command <tt>G0</tt> brings you
back.

## ZX Spectrum mode

The system cassette holds <tt>ZX.COM</tt>. Run it <b>from the volume where its files
are</b>:

<pre>B:
ZX</pre>

From <tt>A&gt;B:ZX</tt> you get &laquo;файл не найден&raquo; (file not found): the program
builds the names of its files without touching the drive letter.

After a few seconds &laquo;© 1982 Sinclair Research Ltd&raquo; appears on the screen, and
from then on it is an ordinary Spectrum.

<b>At first there is no keyboard: Ф10 (F10) turns it on</b>, as on the real machine.

The layout is taken from the machine's manual: the Spectrum's key grid is laid over the
Argo keyboard by position, and the letters match by themselves. The special keys:

| Spectrum | Argo |
|----------|------|
| CAPS SHIFT | ДОП |
| SYMBOL SHIFT | Б (comma) |
| SPACE / BREAK | Ю (period) |

The Argo's own space bar and its Shift do not work in this mode.

<b>Reset</b> in ZX mode returns to the Spectrum's start screen. A cold restart returns the
machine to the monitor.

### Loading games

1. Insert a <tt>.tap</tt> or <tt>.tzx</tt> into the tape recorder;
2. press Ф10 if you have not done so yet;
3. type <tt>load ""</tt> (the J key gives <tt>LOAD</tt>, the quote is ДОП together with P)
   and press ВВОД (Enter);
4. start the tape: with <b>the up arrow</b> on the keyboard or with the button in the tape
   recorder window.

The arrows drive the tape transport bypassing the processor, so they work in a running game
too: up is play, down is stop, left is rewind, right is fast forward.

### Saving games and programs

<tt>SAVE</tt> works. Insert a blank cassette (<tt>software/unior/blank.tap</tt> is an empty
<tt>.tap</tt> file), press &laquo;Record&raquo; in the tape recorder window and give
<tt>save "имя"</tt> (имя stands for the name). The recording is saved as an ordinary
<tt>.tap</tt>, which other Spectrum emulators can read too.

Recording erases everything on the cassette past the head, as on a real tape recorder.

## Monitor commands

A single Latin letter from <tt>C</tt> to <tt>M</tt>:

| | |
|---|---|
| <tt>C</tt> | compare memory areas |
| <tt>D</tt> | display a dump |
| <tt>E</tt> | write to tape |
| <tt>F</tt> | fill an area |
| <tt>G</tt> | run from an address |
| <tt>H</tt> | set a register |
| <tt>I</tt> | read a record from the tape and run it |
| <tt>J</tt> | find and replace |
| <tt>K</tt>, <tt>M</tt> | exchange with a block of the extra memory |
| <tt>L</tt> | load the system from block 3 and run it |

## Keyboard

There is an 88-key on-screen keyboard. On the host, letters, digits and symbols are in
their places; ВК: Enter, arrows: arrows, ЗБ: Backspace (it is also the delete key of
TCP/M). Modifiers: НР &ndash; Shift, УПР &ndash; Ctrl, ДОП &ndash; Alt.

Under TCP/M the symbols are laid out as on a PC: without НР the digits, `,` `.` `/` `;` `'`
`-` `=` `[` `]`, with НР `!` `@` `#` ... `<` `>` `?` `:` `"` `_` `+` `{` `}`. Type them as
on the host: the emulator presses and releases НР by itself.

## File support

* <tt>.bt</tt> &ndash; Argo and Unior tape;
* <tt>.tap</tt>, <tt>.tzx</tt> &ndash; ZX Spectrum tape, for the ZX mode. Standard-speed
  blocks are read from <tt>.tzx</tt>; a tape with a non-standard loader is rejected at
  once, not halfway through.

## Links

* [Emuverse page](https://emuverse.ru/wiki/Арго_ФВ-6511)
* [Wikipedia page](https://ru.wikipedia.org/wiki/%D0%90%D1%80%D0%B3%D0%BE_%D0%A4%D0%92-6511)
* [Dumps and tapes](https://alemorf.github.io/retro_computers/computer.html?id=Argo_FV-6511)
