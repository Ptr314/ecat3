# Apogey BK-01Ts

&laquo;Apogey&raquo; &ndash; one of the improved factory-made clones of the &laquo;Radio-86RK&raquo;.
The main differences &ndash; a ROM extended by 2 KB that can write and read MSX-format cassettes,
a KR580VI53 timer for sound generation, an extended character generator,
and in the model with the index &laquo;Ts&raquo; &ndash; colour attributes of characters.

## Using the computer

### Option 1: Running from the ROM disk
A ROM disk with a set of programs is connected to the computer.

The shell is loaded from the ROM disk with these commands<br/>
<tt><b>--&gt;R,100</b></tt><br/>
<tt><b>--&gt;G</b></tt>

Then choose and start the program you need in the shell.

### Option 2: Direct loading into memory.

1. Load the file through the emulator menu File/Open;
2. Run it with the monitor directive G:<br/>
<tt><b>--&gt;G</b></tt><br/>
or, if the program does not load at address 0000:<br/>
<tt><b>--&gt;G[address]</b></tt>

Supported files: *.rka. The start address is the first two bytes of the file in direct order (high byte, then low).

### Option 3: Loading through the tape recorder emulator.

1. Open the tape recorder window (a button on the toolbar);
2. Choose a *.rka file;
3. Start playback;
4. While the leader sounds, go to the main emulator window and type the command &laquo;<tt>I</tt>&raquo;:<br/>
<tt><b>--&gt;I</b></tt>
5. When loading is complete, run the program as in Option 2. The start address you need is printed on the first line after a successful load.


## Saving files to tape

1. Open the tape recorder window and turn recording on (the button with the red dot);
2. Give the monitor directive O with the bounds of the memory block:<br/>
<tt><b>--&gt;O[start],[end]</b></tt><br/>
The monitor prints the checksum and returns to the prompt;
3. Turn recording off &ndash; the emulator offers to save what was recorded under the name
<tt>tape.rka</tt>. Keep this extension: the emulator uses it to choose how to
put the file back on tape.

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

* [Wikipedia article](http://ru.wikipedia.org/wiki/Апогей_БК-01)
* [Page on zx-pk.ru](https://zx-pk.ru/wiki/%D0%90%D0%BF%D0%BE%D0%B3%D0%B5%D0%B9_%D0%91%D0%9A-01)

Technical documentation and software:

* [On the Emuverse.ru site](https://emuverse.ru/wiki/Апогей_БК-01)

The ROM disk project:

* [Repository](https://github.com/Dovgalyuk/ApogeyROMDisk)
* [Page on zx-pk.ru](https://zx-pk.ru/wiki/ROM-диск_для_компьютера_Апогей-БК01)
