# UKNC memory test

A debug configuration for testing the UKNC memory. No ROM images are needed:
the script puts everything required into memory. There is no real machine here &mdash;
the processor is stopped.

Three mechanisms that all the memory of the machine rests on are tested:

* **plane-pair** &mdash; the address space of the central processor over
  planes 1 and 2: the word at address A is byte A/2 of plane 1 plus byte A/2 of plane 2,
  so 64 KB of addresses take 32 KB of each plane;
* **indirect-memory** &mdash; indirect access of the peripheral processor to plane 0
  and to the memory of the central one, with a latch that is updated only by a write to the
  address register;
* **or_read** &mdash; a window where ROM and RAM answer a read at the same time, and
  the bus sees them ORed together; a write then reaches only the RAM.

Look not at the screen but at the log of the script
`tests/scripts/boot-uknc-memory.ecat`.
