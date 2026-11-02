    .section .text.entry
    .globl _start
_start:
    li t0, 0
1:  addi t0, t0, 1
    xori t1, t0, 0x55
    jal zero, 1b
