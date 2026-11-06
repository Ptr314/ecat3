# UKNC reset test

A debug configuration: one processor holds the other in reset.

On a real UKNC the ROM sits on the side of the peripheral processor, and it is the one that
brings the machine up &mdash; the central one is started by bit 5 of register 177716
of the peripheral one. While the `~dclo` line is active (zero), the central processor does not
execute a single instruction; once released, the line starts it from the start address.

In the configuration the central processor runs a counting loop, and the register bit
is moved by the script `tests/scripts/boot-uknc-reset.ecat`: the counter must stand still
while the line is held down, and run after it has been released.
