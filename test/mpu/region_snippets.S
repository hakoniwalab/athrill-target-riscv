/*
 * Loads around the 1 KiB region at 0x40100200 (test/mpu/memory.txt),
 * which starts in the middle of a 1 KiB page. Each ends with ECALL.
 */
    .section .text
    .align 2
    .globl load_inside_region
load_inside_region:
    li t1, 0x40100200
    lw t2, 0(t1)
    ecall

    .globl load_before_region
load_before_region:
    li t1, 0x40100100
    .globl load_before_region_insn
load_before_region_insn:
    lw t2, 0(t1)
    ecall

    .globl load_after_region
load_after_region:
    li t1, 0x40100600
    .globl load_after_region_insn
load_after_region_insn:
    lw t2, 0(t1)
    ecall

    .globl load_across_region_end
load_across_region_end:
    li t1, 0x401005fe
    .globl load_across_region_end_insn
load_across_region_end_insn:
    lw t2, 0(t1)
    ecall
