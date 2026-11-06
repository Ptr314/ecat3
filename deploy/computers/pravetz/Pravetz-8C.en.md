# Pravetz-8C

The last mass-produced model of the series (1989&ndash;1994): 128 KB on the main board; a 140 KB floppy controller, Centronics and RS-232 ports and an RGB output are built in, and only slots 4, 5 and 7 are free. The ROM is the same as on the Pravetz-8A, with all its differences from the Apple //e (see the Pravetz-8A description).

Differences from the Pravetz-8A:

* A 14.31818 MHz crystal, as on the American //e: 60 frames a second, 262 lines.
* The programs of the built-in devices are in the ROM D38: Centronics ($C100), RS-232 on an 8250 ($C200, registers $C098&ndash;$C09F), the floppy drive loader ($C600). The emulated variant is that of the Tashkent СП «Вариант» (the Variant joint venture).
* A built-in RGB card (selected in the «Video output» list on the toolbar: «RGB card, Pravetz stock»).

Ports:

* Centronics: `PR#1` sends output to the printer. What is printed is shown in the &laquo;Emulation / I/O ports&raquo; window, from which it can be saved to a file.
* COMport in slot 5 (a card of СП «Вариант», 1996): disks on an IBM PC, which the emulator itself plays here. `PR#5` loads DOS from image 1 (the DOS for the card from the COMPORT package), then `D1`, `D2`... are 140 KB images selected in the extension editor (more in the description of the Pravetz-8A with all cards).
* RS-232 (8250): `PR#2` starts the terminal from the ROM (1200 baud, 8N1; exit with the &laquo;:&raquo; key). The port is taken out to a serial port of the PC (a parameter in the extension editor), and the exchange is shown in the ports window.

A PC-style keyboard, with БДС (the Bulgarian standard) or ГОСТ (the Soviet standard) Cyrillic. The floppy drive is as on the Pravetz-8A, with the same school disk with a menu (other disks are in `software/pravetz`). Besides the character generator, the jumper X20 and the layout (ГОСТ, БДС), the extension editor selects the KNGMD (floppy controller) loader (page $600 of D38: the factory one for 16 sectors or a rework for 13 for DOS 3.2 disks, `.d13`) and the ROM D38: the factory one or with the Bulgarian patch E8 (Centronics prints the 8-bit character set).
