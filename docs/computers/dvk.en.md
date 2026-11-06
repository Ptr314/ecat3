# DVK

[Русский](dvk.md) | [Български](dvk.bg.md)

DVK (dialogue computing complexes) on processors of the 1801 series (the PDP-11 instruction set). The «ДВК» group in the machine selection window. Numbers in the configurations and scripts are octal.

## Configurations

| Configuration | Processor board | Terminal | Storage | Description |
|---|---|---|---|---|
| DVK-1 | MS1201.01 (K1801VM1) | 15IE-00-013 | none; BASIC or FOCAL in ROM | [DVK-1.en.md](../../deploy/computers/dvk/DVK-1.en.md) |
| DVK-2 | MS1201.01 | 15IE-00-013 | two 8" GMD-7012 (DX, RX11) | [DVK-2.en.md](../../deploy/computers/dvk/DVK-2.en.md) |
| DVK-3 | MS1201.02 (KM1801VM2) | KSM + KGD, MS7004 | MX, MY, DW hard disk | [DVK-3.en.md](../../deploy/computers/dvk/DVK-3.en.md) |
| DVK-4 (Kvant-4S) | MS1201.04 (KM1801VM3, 1 MB) | KTsGD (800x480, 16 colours, mouse), MS7004 | MY, MX, DW hard disk | [DVK-4.en.md](../../deploy/computers/dvk/DVK-4.en.md) |

Debugging configuration: «К1801ВМ3, тесты DEC» (K1801VM3, DEC tests).

The configuration editor chooses: the DVK-1 user ROM (BASIC 013 or FOCAL 058), the 15IE firmware (5 or 6 chips), the monitor ROM of the DVK-3 (279 or 055) and of the DVK-4 (134 or 377), the KTsGD firmware (181, 182, 182 with KeyGP), the SA1 switch of the KMD board (the MY controller), the host port for the serial port of the DVK-4.

The console switches "Halt" and "Line clock 50 Hz" (interrupts) are buttons on the toolbar.

## What you need

The ROMs (monitors, terminals, KSM, KTsGD, KMD) come with the emulator: `deploy/computers/dvk/files/`. The system disks are in `software/dvk`, the hard disk and the test disk are in the software library on the emulator's site (https://ecat.emuverse.ru/library/dvk/):

| File | Medium | Contents |
|---|---|---|
| `rt11-dx.dsk` | DX (DVK-2) | RT-11SJ V05.04 |
| `rt11-mx.dsk` | MX | RT-11SJ V05.04 with the MX driver |
| `rt11-my.dsk` | MY | RT-11SJ V05.04, the RT-11XM monitor |
| `rt11-dw.dsk` | DW hard disk (5 MB) | RT-11 V05.04, tests, games; inserted by hand; in the library on the site |
| `FODOSTMO.DSK` | MY | FODOS, factory tests (VM2, MY, MX, DW); in the library on the site |

## Booting

| Machine | Monitor command |
|---|---|
| DVK-1 | `140000G` &ndash; the language in ROM |
| DVK-2 | `173000G` |
| DVK-3 | `BMY`, `BMX`, `BDW` |
| DVK-4 | `BMY0`, `BMX0`, `BDW0`; the 377 monitor boots by itself |

## Notes

* After power-on the 15IE is in its own mode; VT52 mode is turned on with a setup bit (see the DVK-1 description).
* DVK-4: the serial port 176570 and the LP11 parallel port 177514; their output is shown in the "Emulation / I/O ports" window.
