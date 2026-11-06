# Kaspiy
## An Irisha clone with its own firmware

In hardware it does not differ from the Irisha &ndash; only the firmware is different.

| Address | Chip | Contents |
|---|---|---|
| $0000&ndash;$07FF | b | monitor, start menu |
| $0800&ndash;$0FFF | not dumped | debugger, taken from the Irisha |
| $1000&ndash;$17FF | not dumped | debugger, taken from the Irisha |
| $1800&ndash;$1FFF | none | the Irisha has its floppy loader here, the Kaspiy does not need it |
| $2000&ndash;$27FF | c1 | monitor |
| $2800&ndash;$2FFF | c2 | monitor |
| $3000&ndash;$37FF | c3 | monitor |
| $3800&ndash;$3FFF | c4 | character generator |

## The debugger is restored from the Irisha

The chips of the range __$0800&ndash;$17FF__ have not been dumped, and _kaspiy.rom_ holds the
matching part of the Irisha firmware in their place. This is a restoration, not a dump, but a well-founded one.

## Keyboard:

* РУС/ЛАТ (Russian/Latin): Caps Lock.
* ВК (carriage return): Enter, ПС (line feed): F12, ГТ (tab): Tab, АР2 (escape): Esc, arrows: arrows.
* СУ (control): Ctrl, Shift &mdash; upper-case characters.
* Letter, digit and symbol keys &mdash; as on a standard QWERTY keyboard with the English layout.
* Ctrl-Break &ndash; reset, Ctrl-Alt-Break &ndash; cold restart.

## Floppy drives

The Kaspiy had no floppy controller of its own. The machines of such a classroom were connected to a
common switching unit, the US-10 interface unit, and the floppy unit was connected to that; a classroom
could have up to 16 workstations.
This is not implemented in the emulator at the moment.

## Other

More about the base machine:

* [Wikipedia article on the Irisha (in Russian)](https://ru.wikipedia.org/wiki/Ириша)
* [On Emuverse.ru](https://emuverse.ru/wiki/Ириша)
