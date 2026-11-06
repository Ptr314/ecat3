# UKNC display test

A debug configuration for testing the display controller. No ROM images are
needed: the script `tests/scripts/boot-uknc-display.ecat` puts the line table and the
pixels themselves into plane 0, and the processors spin in an idle loop.

The video controller walks a linked list of per-line descriptors (the «line
table») in plane 0 starting at address `0000270`. Each line names the address of its pixels,
and four-word descriptors change the palette, stretch, brightness and
cursor as the beam goes &mdash; so every line can have its own eight colours and its own
width.

A pixel is three bits, one from each plane, read at one and the
same address: plane 0 &mdash; the low bit of the colour, plane 2 &mdash; the high one, and
bit 0 of a byte &mdash; the leftmost pixel of the octet. So the bytes `252`, `314` and
`360` in planes 0, 1 and 2 give all eight colours in a row.

The script builds a table of four descriptors &mdash; a two-word one, a palette one,
a parameter one and one that loops back to itself &mdash; and takes two screenshots: with
stretch 1 (640 pixels per line) and with stretch 4 (160 pixels, each four times
as wide).
