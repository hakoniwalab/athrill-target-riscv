/*
 * When CSRRS/CSRRC write (RISC-V privileged specification, Zicsr).
 *
 * CSRRS and CSRRC write the CSR unless the rs1 field is x0, and CSRRSI and
 * CSRRCI unless uimm is 0 - independently of the value being set or
 * cleared. A write to a read-only CSR (mhartid) is an illegal instruction.
 * The last line is "SELFTEST PASS" or "SELFTEST FAIL <count>".
 */
#include <stdint.h>

#define SERIAL (*(volatile uint8_t *)0x10000000UL)

#define MSTATUS_MPP_MACHINE 0x1800U
#define CAUSE_ILLEGAL_INSTRUCTION 2U
#define CAUSE_ECALL_FROM_MACHINE 11U

static void test_putc(char c)
{
	SERIAL = (uint8_t)c;
}

#include "../common/test_report.h"

extern uint64_t run_until_trap(uint64_t entry, uint64_t mpp_bits);
extern volatile uint64_t trap_epc;
extern char csrrs_x0_source[];
extern char csrrs_zero_valued_register[];
extern char csrrs_zero_valued_register_insn[];
extern char csrrc_zero_valued_register[];
extern char csrrc_zero_valued_register_insn[];
extern char csrrsi_zero_immediate[];
extern char csrrci_nonzero_immediate[];

static uint64_t run(char *entry)
{
	return run_until_trap((uint64_t)(uintptr_t)entry, MSTATUS_MPP_MACHINE);
}

static int is_illegal_at(uint64_t cause, char *instruction)
{
	return cause == CAUSE_ILLEGAL_INSTRUCTION
		&& trap_epc == (uint64_t)(uintptr_t)instruction;
}

int main(void)
{
	test_check("csrrs rs1=x0 only reads",
		run(csrrs_x0_source) == CAUSE_ECALL_FROM_MACHINE);
	test_check("csrrs with a zero-valued register writes",
		is_illegal_at(run(csrrs_zero_valued_register), csrrs_zero_valued_register_insn));
	test_check("csrrc with a zero-valued register writes",
		is_illegal_at(run(csrrc_zero_valued_register), csrrc_zero_valued_register_insn));
	test_check("csrrsi uimm=0 only reads",
		run(csrrsi_zero_immediate) == CAUSE_ECALL_FROM_MACHINE);
	test_check("csrrci uimm=1 writes",
		is_illegal_at(run(csrrci_nonzero_immediate), csrrci_nonzero_immediate));
	test_report_finish();
	return 0;
}
