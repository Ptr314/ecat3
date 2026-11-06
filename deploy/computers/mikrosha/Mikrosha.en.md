# Mikrosha
## A factory-made version of the Radio-86RK

It differed from the original mainly in a sound generator built on the KR580VI53 timer,
and in a changed connection of the internal devices, the keyboard among them.
Because of this, most Radio-86RK programs do not work on the Mikrosha without changes.

Supported files: *.rkm

## Using the computer

### Loading files:

Option 1 (fast): Direct loading into memory.
1. Load the file through the emulator menu File/Open;
2. Run it with the monitor directive G:<br/>
-->G&lt;Enter&gt;<br/>
or, if the program does not load at address 0000:<br/>
-->G&lt;address&gt;&lt;Enter&gt;

The load address can be seen in the file &ndash; it is the first two bytes of .rkm files.

Option 2 (for connoisseurs): Loading through the tape recorder.
1. Open the tape recorder window and load the file into it;
2. Start playback;
3. Without waiting for the end of the leader, give the monitor directive I:<br/>
-->I&lt;Enter&gt;<br/>
4. Wait for the end of the tape and run the program with the directive G.

The Mikrosha writes and reads tape more slowly than the Radio-86RK &ndash; about 700 baud against
1200 &ndash; so .rkm files are played at their own speed.

## Saving files to tape

1. Open the tape recorder window and turn recording on (the button with the red dot);
2. Give the monitor directive O with the bounds of the memory block:<br/>
<tt><b>--&gt;O[start],[end]</b></tt><br/>
The monitor prints the checksum and returns to the prompt;
3. Turn recording off &ndash; the emulator offers to save what was recorded under the name
<tt>tape.rkm</tt>. Keep this extension: the emulator uses it to choose how to
put the file back on tape.

### Useful monitor directives:
* G&lt;address&gt; &ndash; runs the program at &lt;address&gt;;
* D&lt;start&gt;,&lt;end&gt; &ndash; shows the contents of a memory block;
* M&lt;address&gt; &ndash; shows and changes the contents of a memory cell. Editing ends with the character "." (a period).

## Keyboard:

* РУС/ЛАТ (Cyrillic/Latin): Caps Lock.
* НР: Shift, УС: Ctrl.
* ВК (carriage return): Enter, ПС (line feed): Enter on the numeric keypad.
* ТАБ: Tab, АР2: Esc, СТР (clear screen): Delete, &#8598;: Home.
* F1&ndash;F5 and the arrows: the keys of the same name.
* Letter, digit and symbol keys &mdash; as on a standard QWERTY keyboard with the English layout.
* Ctrl-Break &ndash; reset, Ctrl-Alt-Break &ndash; cold restart.
* The on-screen keyboard is available.

## Known problem

Switching to the Russian layout produces a continuous tone, as on the real machine.
Pressing any key stops the sound.

The cause is that the Mikrosha uses one port both for the keyboard and for the sound.
Presumably this is a flaw of the machine's original design.

## Other

More about the computer:

* [Wikipedia article](http://ru.wikipedia.org/wiki/Микроша)

Technical documentation and software:

* [On the Emuverse.ru site](http://www.emuverse.ru/wiki/Микроша)
