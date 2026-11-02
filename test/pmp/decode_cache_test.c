/*
 * Decode-cache reuse across privilege and PMP changes.
 *
 * A two-instruction probe sits in its own PMP region. Machine mode runs it
 * first, which puts its instructions into the decode cache. User mode then
 * jumps to the same address: whether the fetch is allowed must follow the
 * current privilege and the current PMP setting, not what was true when the
 * cache entry was filled.
 *
 * Expected results come from the privileged specification:
 *   - an unlocked PMP entry does not restrict Machine mode;
 *   - a User fetch from a region without X raises an instruction access
 *     fault (mcause 1) with mepc at the probe;
 *   - an ECALL from User is mcause 8, from Machine mcause 11;
 *   - MRET from User is an illegal instruction (mcause 2).
 * The last line is "SELFTEST PASS" or "SELFTEST FAIL <count>".
 */
#include <stdint.h>

#define SERIAL (*(volatile uint8_t *)0x10000000UL)

#define MSTATUS_MPP_USER 0x0000U
#define MSTATUS_MPP_MACHINE 0x1800U

#define CAUSE_FETCH_ACCESS_FAULT 1U
#define CAUSE_ILLEGAL_INSTRUCTION 2U
#define CAUSE_ECALL_FROM_USER 8U
#define CAUSE_ECALL_FROM_MACHINE 11U

#define PMP_TOR_RWX 0x0fU
#define PMP_TOR_RW 0x0bU
#define PMP_ADDRESS_TOP 0x003fffffffffffffULL

static void test_putc(char c)
{
	SERIAL = (uint8_t)c;
}

#include "../common/test_report.h"

extern uint64_t run_until_trap(uint64_t entry, uint64_t mpp_bits);
extern char probe_start[];
extern char probe_end[];
extern char user_mret[];
extern volatile uint64_t trap_epc;

/*
 * Three TOR entries: everything below the probe and everything above it is
 * RWX for User; the probe region gets the given permission.
 */
static void set_pmp(uint8_t probe_permission)
{
	uint64_t config = (uint64_t)PMP_TOR_RWX
		| ((uint64_t)probe_permission << 8)
		| ((uint64_t)PMP_TOR_RWX << 16);

	asm volatile("csrw pmpaddr0, %0" : : "r"((uint64_t)(uintptr_t)probe_start >> 2));
	asm volatile("csrw pmpaddr1, %0" : : "r"((uint64_t)(uintptr_t)probe_end >> 2));
	asm volatile("csrw pmpaddr2, %0" : : "r"(PMP_ADDRESS_TOP));
	asm volatile("csrw pmpcfg0, %0" : : "r"(config));
}

static uint64_t run_probe(uint64_t mpp_bits)
{
	return run_until_trap((uint64_t)(uintptr_t)probe_start, mpp_bits);
}

static int is_fetch_fault_at_probe(uint64_t cause)
{
	return cause == CAUSE_FETCH_ACCESS_FAULT
		&& trap_epc == (uint64_t)(uintptr_t)probe_start;
}

int main(void)
{
	set_pmp(PMP_TOR_RW);
	test_check("machine runs probe without X",
		run_probe(MSTATUS_MPP_MACHINE) == CAUSE_ECALL_FROM_MACHINE);
	/* Same address, same PMP, cached by Machine: only the privilege differs. */
	test_check("user fetch denied after machine filled cache",
		is_fetch_fault_at_probe(run_probe(MSTATUS_MPP_USER)));

	set_pmp(PMP_TOR_RWX);
	test_check("user runs probe after X granted",
		run_probe(MSTATUS_MPP_USER) == CAUSE_ECALL_FROM_USER);
	/* Same address, same privilege, cached by User: only PMP differs. */
	set_pmp(PMP_TOR_RW);
	test_check("user fetch denied after X revoked",
		is_fetch_fault_at_probe(run_probe(MSTATUS_MPP_USER)));
	test_check("machine still runs probe",
		run_probe(MSTATUS_MPP_MACHINE) == CAUSE_ECALL_FROM_MACHINE);
	test_check("user fetch denied after machine ran again",
		is_fetch_fault_at_probe(run_probe(MSTATUS_MPP_USER)));

	test_check("mret from user is illegal",
		run_until_trap((uint64_t)(uintptr_t)user_mret, MSTATUS_MPP_USER)
			== CAUSE_ILLEGAL_INSTRUCTION
		&& trap_epc == (uint64_t)(uintptr_t)user_mret);

	test_report_finish();
	return 0;
}
