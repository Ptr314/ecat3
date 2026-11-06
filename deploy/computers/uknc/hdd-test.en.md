# UKNC hard disk test

A debug configuration for testing the IDE controller. No ROM images are needed:
there is no machine here, the script is in charge of the controller registers. The processor
spins on a `BR .` instruction &mdash; only so that emulated time
runs: the drive does not answer at once, and without time running a sector read
would never finish.

What is tested is what the exchange with a hard disk is made of:

* **register select** &mdash; bits 1&ndash;3 of the address, inverted: 110000
  is the status and the command, 110016 &mdash; the data;
* **the inverted bus** &mdash; the machine reads and writes the registers inverted,
  and a sector arrives as it lies on the drive;
* **conversion of cylinder-head-sector into an offset** &mdash; sectors are numbered from
  one, the geometry is taken from the first sector of the image;
* **status bits** &mdash; «busy», «buffer ready», «error», and the time
  the drive spends seeking a track.

The image `tests/files/uknc-hdd.img` is made by `tests/files/make_uknc_hdd.py`: 4
sectors, 2 heads, 8 cylinders, 32 KB in all. Each sector holds its own
number, so an error in the address conversion shows at once.

Look not at the screen but at the log of the script
`tests/scripts/boot-uknc-hdd.ecat`.
