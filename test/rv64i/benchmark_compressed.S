    .section .text.entry
    .globl _start
    .option rvc
_start:
    li t0, 0
1:  c.addi t0, 1
    xori t1, t0, 0x55
    c.j 1b
