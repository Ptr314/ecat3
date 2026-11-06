## Radio-86RK
## The classic version with 32 KB of memory.

Supported files: *.rk, *.rkr, *.gam.

## Loading files:

### Option 1 (fast): Direct loading into memory.

1. Load the file through the emulator menu File/Open;
2. Run it with the monitor directive G:<br/>
<tt><b>--&gt;G</b></tt><br/>
or, if the program does not load at address 0000:<br/>
<tt><b>--&gt;G[address]</b></tt>

The load address can be seen in the file &ndash; it is the first two bytes of .rk and .rkr files, or the second and third bytes of a .gam file.

### Option 2: Loading through the tape recorder emulator.

1. Open the tape recorder window (a button on the toolbar);
2. Choose the file;
3. Start playback;
4. While the leader sounds, go to the main emulator window and type the command<br/>
<tt><b>--&gt;I</b></tt>
5. When loading is complete, run the program as in Option 1. The start address you need is printed on the first line after a successful load.

## Saving files to tape

1. Open the tape recorder window and turn recording on (the button with the red dot);
2. Give the monitor directive O with the bounds of the memory block:<br/>
<tt><b>--&gt;O[start],[end]</b></tt><br/>
The monitor prints the checksum and returns to the prompt;
3. Turn recording off &ndash; the emulator offers to save what was recorded under the name
<tt>tape.rk</tt>. Keep this extension: the emulator uses it to choose how to
put the file back on tape.

### Useful monitor directives:

* **<tt>G[address]</tt>** &ndash; runs the program at [address];
* **<tt>D[start],[end]</tt>** &ndash; shows the contents of a memory block;
* **<tt>M[address]</tt>** &ndash; shows and changes the contents of a memory cell. Editing ends with the character &laquo;.&raquo; (a period).

## Keyboard:

* РУС/ЛАТ (Cyrillic/Latin): Caps Lock.
* НР: Shift, УС: Ctrl.
* ВК (carriage return): Enter, ПС (line feed): Enter on the numeric keypad.
* ЗБ: Backspace, ТАБ: Tab, АР2: Esc, СТР (clear screen): Delete, &#8598;: Home.
* F1&ndash;F4 and the arrows: the keys of the same name.
* Letter, digit and symbol keys &mdash; as on a standard QWERTY keyboard with the English layout.
* Ctrl-Break &ndash; reset, Ctrl-Alt-Break &ndash; cold restart.
* The on-screen keyboard is available.

## Other

More about the computer:

* [Wikipedia article](http://ru.wikipedia.org/wiki/Радио_86РК)

Technical documentation and software:

* [On the Emuverse.ru site](http://www.emuverse.ru/wiki/Радио-86РК)

