# K1801VM1 test

A debug configuration for checking the K1801VM1 processor core and the BK display
controller. No ROM images are needed: the test program is written right in the
configuration file, in the `data` parameter of the `bios` device.

The program fills the whole video memory with a decrementing counter, then goes into
an endless loop. A regular pattern must appear on the screen:

* in colour mode &mdash; 256x256 dots in the four colours of the BK palette;
* in monochrome mode &mdash; 512x256 dots, twice as fine horizontally.

The mode is switched by the drop-down list on the toolbar.
