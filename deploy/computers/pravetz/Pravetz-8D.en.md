# Pravetz-8D

A Bulgarian home computer of the mid-1980s: a counterpart of the Oric Atmos; only the name relates it to the Apple and the other Pravetz models.
A 6502 processor at 1 MHz, 64 KB of RAM, 16 KB of ROM with the Oric BASIC translated into Cyrillic, an AY-3-8912 sound generator,
the screen on a custom ULA chip.

* Screen: 40 x 28 text or HIRES graphics 240 x 200 with three lines of text at the bottom, eight colours.
  Colour, flashing, double height and the mode are set by attributes, bytes of the picture itself,
  so the colour changes no more often than once a character cell (`INK`, `PAPER`, `HIRES`, `TEXT`).
* Sound: `SOUND`, `MUSIC`, `PLAY`, `PING`, `SHOOT`, `EXPLODE`, `ZAP`.
* Tape recorder: `CSAVE"ИМЯ"` and `CLOAD"ИМЯ"` (ИМЯ is the name; `CLOAD""` loads the first program on the tape).
  Tapes are `.tap` files (as in Oric emulators) and `.wav`; what the machine records is saved to `.tap`.
  The machine controls the motor itself: the tape runs while the ROM reads it.
* Floppy drive: the Pravetz controller (like the Apple's Disk II, at addresses $0310-$031F, with the 8DOS ROM),
  140 KB disks. Booting from a disk: `CALL 800`.
  By default the drive holds the 8D RDOS 2.31 system disk with an empty catalogue: `DIR`, `SAVE"ИМЯ"`, `LOAD"ИМЯ"`.

## Keyboard

The on-screen keyboard is drawn from a photograph of the machine: ОСВ (Esc) at the start of the Q row, МК (Ctrl) at the start of the A row.

* C/L (Cyrillic/Latin): Caps Lock. In Cyrillic the letter keys give А&ndash;Я, and Shift with a letter gives Latin.
* Ю, Ш, Э, Щ, Ч: Shift with @, [, \, ], ^ in any mode; on the PC keyboard, the keys `` ` ``, `{`, `|`, `}`, `~`.
* The alphabet is Bulgarian: Ъ is on the Y key (in place of Ы), and there is no letter Ы.
* МК: Ctrl, DEL: Backspace or Delete.
* RESET: F12. The button is wired to NMI: a warm start that interrupts the program but leaves it in memory.

The characters above the digits are as on the Oric: `'` on 7, `(` on 8, `)` on 9, `=` on minus, `+` on `;`,
`*` on `:`; they can be typed as they are drawn on the PC keyboard.
