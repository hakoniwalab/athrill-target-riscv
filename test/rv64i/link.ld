ENTRY(_start)
MEMORY {
  MRAM (rx) : ORIGIN = 0x40000000, LENGTH = 64K
  RAM (rw)  : ORIGIN = 0x40010000, LENGTH = 64K
}
SECTIONS {
  .text ORIGIN(MRAM) + 0x200 : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) } > MRAM
  .data : { *(.data*) } > RAM
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON); __bss_end = .; } > RAM
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
