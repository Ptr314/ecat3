# DVK-1 (MS1201.01, BASIC or FOCAL, 15IE)

A DVK (dialogue computing complex) on the MS1201.01 processor board
(K1801VM1) with the 15IE-00-013 terminal. There are no floppy drives; the user
ROM holds BASIC (ROM 013) or FOCAL (ROM 058).
BASIC is installed by default; FOCAL is chosen in the configuration editor:
a copy of the machine, the «Пользовательское ПЗУ» (user ROM) field.

After power-on the monitor command

`@140000G`

starts the language in the user ROM.

The line clock on the front panel is off: BASIC keeps its own code at
address 100, where the timer vector would be.

## Terminal and keyboard

The 15IE-00-013 terminal with the 15VVV-97-006 keyboard runs its own
firmware. The firmware is replaceable &mdash; a set of 5 chips (the newer one) or of 6;
it is chosen in the configuration editor, the «Прошивка терминала» (terminal firmware) field.

Above the screen there is a status line: the line speed, 16 setup bits and a clock.
After power-on the terminal is in its own 15IE mode.
VT52 mode (expected, for example, by the SL line editor and the RT-11 screen editors)
is turned on with a setup bit:

1. СДВ (F6 or on the on-screen keyboard) &mdash; the cursor moves into the status line;
2. right arrow seven times &mdash; to the third digit of the second binary group;
3. «Уст. след.» ("set next") on the on-screen keyboard (or Ctrl+B) &mdash; the digit becomes 1;
4. СДВ (F6) &mdash; leave the setup.

The terminal has no non-volatile memory: after power-on all the bits are zero
again. The switches above the keyboard: with ЛИН (line) off the terminal is local
(typing goes to the screen, and it does not read the line); with ДУП (duplex) off it is
half duplex. РЕД (edit) must be pressed for normal work: when it is released, the
terminal does not execute control codes but writes them on the screen, where
they blink (this is how the status line is made: the codes in it are the marks
for the arrows and the parameter numbers).

Other setup bits of the same second group: the second is keyboard auto-repeat
(0 &mdash; on: a key held longer than a second repeats 15 times a second),
the third is KOI-7 N2 (1 &mdash; small Latin letters are shown as capital
Russian ones). The terminal beeps (2400 Hz) on code 007 and on a key press.

* РУС, ЛАТ (Russian, Latin): Caps Lock, as whichever of the two changes the alphabet.
  The keys only send the codes 016 and 017 (with auto-repeat); the terminal itself
  sets the alphabet and the ЛАТ lamp.
* ВК (carriage return): Enter, ЗБ (DEL): Backspace, ПС (line feed): F12, ГТ (tab): Tab, АР2 (ESC): Esc, arrows: arrows.
* СУ (control): Ctrl; Shift gives the upper-case characters.
* Letters and symbols follow the Latin labels of the host keys.
* Switches: СДВ: F6, РЕД: F7, ЛИН: F8, ДУП: F9, ПЧ: F10; each press toggles the switch.
* The on-screen keyboard is available.
