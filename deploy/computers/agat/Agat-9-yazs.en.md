# Agat-9 with a sound synthesis card

The standard version, with 128 KB of memory on the main board, two floppy drives &ndash; 840 KB (slot 5) and 140 KB (slot 6) &ndash; and the [sound synthesis card 5/2](https://agatcomp.ru/agat/Hardware/SoundNCL/jzs52.shtml) (YaZS) of the Novosibirsk Computer Laboratory in slot 1.

## The sound synthesis card

Five tone channels and two percussion channels. Each tone channel is set by its own KR580VI53 counter and by register bits: switching on with a smooth attack, a level pulse, the choice of bands (low, mid, high) and of the filter. The sound of the card is computed from its schematic, so the timbre of a channel depends on the chosen bands just as on a real card. The timer of the card gives interrupts to the processor.

The sound of the card is mixed into the built-in speaker. The `volume` parameter of the `yazs` device sets the volume of the card relative to the speaker (400 by default; it can also be chosen in the configuration editor).

## Demo program

By default the 840 KB drive holds the YaZS demo disk. To start the demo:

* in the IKP menu choose «Ассемблер» (Assembler);
* press РЕД (Esc);
* choose «Выйти в отладчик» (Exit to the debugger) in the menu; the prompt `>` appears;
* type `[RUN DEMO` and press ВК (Enter).

The second drive holds the IKP disk for the 140 KB drive. The 140 KB controller is the second version, made for the Agat-9: the system finds it at boot by the signature in its ROM. IKP numbers the drives from the top slot: ДЗУ 1 (disk drive 1) &ndash; 140 KB (slot 6), ДЗУ 2 &ndash; 840 KB (slot 5). The machine still boots from the 840 KB drive.

## File support

The 840 KB drive takes DSK and AIM images, the 140 KB drive &ndash; DSK, MFM, NIB and NIC.

For information about the computer see the description of the main configuration.

## About the YaZS
* Special thanks to Igor Filatov, who managed to get a working specimen of the card and its software.
* [On agatcomp.ru](https://agatcomp.ru/agat/Hardware/SoundNCL/jzs52.shtml)
* [Schematic and board](https://agat-hardware.sourceforge.io/agat-sound.html) by Oleg Odintsov
