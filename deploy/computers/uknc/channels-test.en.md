# UKNC channel test

A debug configuration: the exchange channels between the processors.

The UKNC has no shared memory, and the only way the processors talk to each other is five
byte-wide channels with handshaking: three from the central processor to the
peripheral one and two back. The system software uses channel 0 for the terminal
(the PPU owns the screen and the keyboard), channel 1 for the printer, channel 2 for commands
and block transfers.

One device sits in both mappers at different bases: the two sides see
different registers at different addresses. The processors execute no instructions here;
the registers are moved by the script `tests/scripts/boot-uknc-channels.ecat` &mdash;
this way the protocol itself is seen, not the way the ROM uses it.
