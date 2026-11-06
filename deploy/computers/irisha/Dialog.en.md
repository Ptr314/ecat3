# Dialog
## An Irisha clone with its own firmware

In hardware it differs little from the Irisha: the RAM map is simpler (there are no additional 64 KB,
all the memory maps show the same memory) and 4 KB of ROM are added &ndash; 20 KB in all instead of 16 KB.
The additional ROM is switched in where the original had the additional RAM, and is seen in memory
map 1 at address $0000.

| Address | Chips | Contents |
|---|---|---|
| $0000&ndash;$07FF | D23 | monitor, start menu |
| $0800&ndash;$1FFF | D30, D29, D28 | IRITEXT text editor |
| $2000&ndash;$3FFF | D24&ndash;D27 | monitor and character generator |
| $4000&ndash;$4FFF | D32, D31 | DDT debugger |

The debugger is almost the same as the Irisha's: it differs from the Irisha's overlay in six bytes.
The original kept this overlay as an image at $0800&ndash;$17FF and copied it to $9000 at start;
here it has a ROM of its own. The editor is copied to $A100.

## Using the computer

After power-on the screen shows a menu. Any key moves to the next item (round in a circle),
the input key (__ВК__, carriage return) starts it:

* **АДР.4000** (ADDR.4000) &ndash; jump to address $4000;
* **МАГНИТОФОН** (TAPE RECORDER) &ndash; load from tape;
* **ОТЛАДЧИК** (DEBUGGER) &ndash; DDT, prompt `>`;
* **РЕДАКТОР** (EDITOR) &ndash; the IRITEXT text editor.

**АДР.4000** is not a program of its own but a start of whatever is at address $4000: the monitor
jumps there. The item is meant for a program loaded from tape or entered with the debugger
beforehand; on a machine just switched on there is nothing there.

## Keyboard:

* РУС/ЛАТ (Russian/Latin): Caps Lock.
* ВК: Enter, ПС (line feed): F12, ГТ (tab): Tab, АР2 (escape): Esc, arrows: arrows.
* СУ (control): Ctrl, Shift &mdash; upper-case characters.
* Letter, digit and symbol keys &mdash; as on a standard QWERTY keyboard with the English layout.
* Ctrl-Break &ndash; reset, Ctrl-Alt-Break &ndash; cold restart.

## Other

More about the base machine:

* [Wikipedia article on the Irisha (in Russian)](https://ru.wikipedia.org/wiki/Ириша)
* [On Emuverse.ru](https://emuverse.ru/wiki/Ириша)
