OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0x40000000, LENGTH = 16M }
SECTIONS {
  . = ORIGIN(ram);
  .text : { KEEP(*(.text.entry)) *(.text*) } > ram
  .rodata : { *(.rodata*) } > ram
  .data : { *(.data*) *(.sdata*) } > ram
  .bss : { *(.bss*) *(.sbss*) *(COMMON) } > ram
  . = ALIGN(16);
  __stack_top = ORIGIN(ram) + LENGTH(ram) - 16;
}
