# Pravetz-8M

A development of the Pravetz-82 (1985&ndash;1987): 64 KB on the main board, its own ROM and a character generator with four character sets, a built-in CP/M card with a Z80 processor. A keyboard with a DEL key and two LEDs.

## Differences from the Apple ][+

* 64 KB on the main board instead of 48 KB.
* Its own ROM.
* A character generator with Cyrillic, four sets of 2 KB.
* ЯВЕРТЪ keyboard (the Bulgarian phonetic layout) with a DEL key, the КИР (Cyrillic) alphabet switch and auto-repeat.
* A built-in CP/M card with a Z80.

The CP/M card is not emulated yet. The character set is selected with the `display:font_half` parameter (0&ndash;3); the default is the one compatible with the Pravetz-82.

The keyboard and the floppy drive are as on the Pravetz-82, the Cyrillic is ЯВЕРТЪ on the codes of the 8A. On the on-screen keyboard the КИР keys at the bottom left and right are Shift, and the КИР in the third row switches the alphabet (the КИР lamp).
