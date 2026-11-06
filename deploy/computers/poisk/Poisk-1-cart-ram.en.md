# Poisk-1 + RAM cartridge

The Poisk-1 with 128 KB on the board and a 256 KB memory expansion cartridge at addresses
18000-57FFF, right after the visible part of the board RAM: the BIOS finds 352 KB.

The cartridge memory sits on its own bus and the video controller does not read it, so the
processor works with it without wait states, while the board RAM slows the processor down
during picture output. Games (Prince of Persia) used this by moving themselves into the
cartridge memory.

The size of the cartridge is only an example here.
