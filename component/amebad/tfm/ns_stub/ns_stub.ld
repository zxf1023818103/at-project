ENTRY(ns_reset)
MEMORY { NSRAM (rwx) : ORIGIN = 0x10010000, LENGTH = 0x4000 }
SECTIONS {
  .text 0x10010000 : {
    KEEP(*(.ns_vectors))
    *(.text*) *(.rodata*)
  } > NSRAM
}
