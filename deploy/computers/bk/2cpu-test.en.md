# Two-processor test

A debug configuration for checking machines with several processors. No ROM images
are needed: the test program is written right in the configuration file, in the
`data` parameter.

The machine has two K1801VM1 processors at 8 and 6.25 MHz &mdash; the same 32:25 ratio
as between the central and peripheral processors of the UKNC. There is no shared memory:
each one has its own address mapper (`mapper` and `ppu-mapper`), its own RAM and its own
copy of the program.

Both processors spin the same loop:

~~~
	INC @#100
	BR  .-4
~~~

So after the same wait the counters at address 0100 in `ram` and `ppu-ram` must
relate as the frequencies, 32 to 25. The readings of two timers with the same divider
diverge in the same way: `timer` is clocked from the master processor, `ppu-timer`
&mdash; from the slave one, through the `clock_source` parameter.

The configuration has no screen in the usual sense: `display` is there only because
a device with that name is required, and it shows the contents of uninitialised
video memory. Do not look at it; look at the log of the script
`tests/scripts/boot-2cpu.ecat`.

The configuration is a debug one (`debug = 1`), so it is not shown in the common
list of machines: the `show_debug_versions` parameter in the ini file turns its
display on.
