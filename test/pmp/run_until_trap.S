/*
 * Run code at a given privilege level until it traps, then come back.
 *
 *   uint64_t run_until_trap(uint64_t entry, uint64_t mpp_bits);
 *
 * mpp_bits is the value of mstatus.MPP already shifted into place
 * (0 for User, 0x1800 for Machine). The routine enters `entry` with MRET;
 * the first trap restores the caller's stack and returns mcause, and the
 * trap's mepc is left in trap_epc.
 */
    .section .text
    .globl run_until_trap
run_until_trap:
    la t0, saved_sp
    sd sp, 0(t0)
    la t0, saved_ra
    sd ra, 0(t0)
    la t0, trap_entry
    csrw mtvec, t0
    csrw mepc, a0
    li t0, 0x1800
    csrc mstatus, t0
    csrs mstatus, a1
    mret

    .align 2
trap_entry:
    la t0, saved_sp
    ld sp, 0(t0)
    la t0, saved_ra
    ld ra, 0(t0)
    csrr t0, mepc
    la t1, trap_epc
    sd t0, 0(t1)
    csrr a0, mcause
    ret

/* MRET is a Machine-mode instruction: from User it is illegal. */
    .section .text
    .align 2
    .globl user_mret
user_mret:
    mret

/*
 * The probe: two instructions in their own PMP region. Reaching the ECALL
 * means the probe was fetched and executed.
 */
    .section .text.probe
    .align 4
    .globl probe_start
    .globl probe_end
probe_start:
    addi t2, zero, 0x55
    ecall
    .align 4
probe_end:

    .section .bss
    .align 3
saved_sp:
    .dword 0
saved_ra:
    .dword 0
    .globl trap_epc
trap_epc:
    .dword 0
