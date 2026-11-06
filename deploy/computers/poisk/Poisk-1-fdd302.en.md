# Poisk-1, floppy ROM v3.02

The Poisk-1 with 512 KB whose B504 floppy cartridge has not the factory
ROM but a widespread modification: «Additional ROM BIOS v3.02 for
DISKETTE service (c) Moscow 1991».

Before every access to drive A: the factory driver waits about a quarter of a second for the
motor to spin up (its «motor already on» check does not work for A:),
and MS-DOS takes about a minute to boot. Version 3.02 waits only when the motor really is
stopped, so booting is several times faster.
