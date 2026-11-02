.section .text.start
.globl _start
_start:
  li sp, 0x41000000
  la t0, trap_handler
  csrw mtvec, t0
  call main
1: wfi
  j 1b
