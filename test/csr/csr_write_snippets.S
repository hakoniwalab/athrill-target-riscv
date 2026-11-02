/*
 * One CSR instruction each, followed by ECALL. mhartid (0xf14) is
 * read-only, so a write attempt traps (illegal instruction, cause 2) and a
 * plain read reaches the ECALL (cause 11 from Machine mode).
 */
    .section .text
    .align 2
    .globl csrrs_x0_source
csrrs_x0_source:
    csrrs t0, mhartid, x0
    ecall

    .globl csrrs_zero_valued_register
csrrs_zero_valued_register:
    li t1, 0
    .globl csrrs_zero_valued_register_insn
csrrs_zero_valued_register_insn:
    csrrs t0, mhartid, t1
    ecall

    .globl csrrc_zero_valued_register
csrrc_zero_valued_register:
    li t1, 0
    .globl csrrc_zero_valued_register_insn
csrrc_zero_valued_register_insn:
    csrrc t0, mhartid, t1
    ecall

    .globl csrrsi_zero_immediate
csrrsi_zero_immediate:
    csrrsi t0, mhartid, 0
    ecall

    .globl csrrci_nonzero_immediate
csrrci_nonzero_immediate:
    csrrci t0, mhartid, 1
    ecall
