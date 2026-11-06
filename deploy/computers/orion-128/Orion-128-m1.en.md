# Orion-128
## The original magazine version with Monitor-1

The version from the &laquo;Radio&raquo; magazine of 1990. This configuration is included for historical reasons: it is inconvenient to work with, and many later programs are not compatible with M1.

Supported files: *.rko, *.bru, *.ord.

## Using the computer

### Loading files:

Option 1 (the only one): Direct loading into memory.

1. Load the file through the emulator menu __File/Open__;
2. Run it with the monitor directive G:<br/>
<b>--&gt;G</b><br/>
or, if the program does not load at address 0000:<br/>
<b>--&gt;G[address]</b>

The load address can be seen in the file &ndash; it is the two bytes at offset $55 in .rko files, or at offset 8 in a .bru file.

Working in Monitor-1 is described in the corresponding [article in the &laquo;Radio&raquo; magazine](https://emuverse.ru/wiki/Орион-128/Радио_02-90/Программное_обеспечение).

### Useful Monitor-1 directives:

The syntax differs from that of the Radio-86RK, where the second parameter is the end address.

* **<tt>G[address]</tt>** &ndash; runs the program at [address];
* **<tt>D[address][,page]</tt>** &ndash; memory dump. There is no end address: 256 bytes are shown
  at a time, ВК (carriage return) shows the next portion, a period stops the output. After the comma comes
  the number of the memory page (1&ndash;3), not the end of the block: <tt>D0000,001F</tt> asks for a page
  that does not exist and prints nothing but $FF;
* **<tt>I</tt>** &ndash; reading from tape, **<tt>O[start],[end]</tt>** &ndash; writing to it.

Known problem: after writing to tape with the directive O, the monitor stops recognising directives
and answers «?» to any of them. The emulator takes the РУС/ЛАТ (Cyrillic/Latin) state from the indicator
line (bit 3 of port C), while writing to tape drives data bytes through this port and leaves the indicator
in a state that depends on the last byte written; the monitor itself stays in its previous mode. A key
then comes from the other half of the layout: instead of <tt>I</tt> ($49) the monitor gets $5B. Two
presses of РУС/ЛАТ cure it &ndash; the first brings the emulator and the monitor into agreement, the second
returns to ЛАТ. The Radio-86RK and the Mikrosha do not suffer from this: their ROM restores the indicator
from its own variable on every keyboard scan.

In theory, ORDOS can be loaded from the ROM disk with the directive R, but at present this does not work correctly. The cause &ndash; emulation errors or programs incompatible with M1 &ndash; is not known yet.

## Keyboard:

* РУС/ЛАТ: Caps Lock.
* НР: Shift, УС: Ctrl.
* ВК: Enter, ПС (line feed): Enter on the numeric keypad.
* ЗБ: Backspace, ТАБ: Tab, АР2: Esc, СТР (clear screen): Delete, &#8598;: Home.
* F1&ndash;F4 and the arrows: the keys of the same name.
* Letter, digit and symbol keys &mdash; as on a standard QWERTY keyboard with the English layout.
* Ctrl-Break &ndash; reset, Ctrl-Alt-Break &ndash; cold restart.
* The machine's keyboard can be opened as a picture: the menu «Emulation &rarr; Keyboard».

## Other

More about the computer:

* [Wikipedia article](https://ru.wikipedia.org/wiki/Орион-128)

Technical documentation and software:

* [On the Emuverse.ru site](https://emuverse.ru/wiki/Орион-128)

