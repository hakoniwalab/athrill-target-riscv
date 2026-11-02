/*
 * Instruction fetch at the end of a memory region.
 *
 * test/fetch/memory.txt maps two 4 KiB regions with nothing after them:
 * 0x40100000 (alone) and 0x40200000 (followed directly by 0x40201000).
 * The test writes single instructions near their ends and runs each in
 * Machine mode until it traps.
 *
 * Expected results (the fetch follows the memory map, as on hardware):
 *   - instructions that lie fully inside the region run (EBREAK, cause 3);
 *   - a 32-bit instruction whose upper half is unmapped is an instruction
 *     access fault (cause 1) with mepc at that instruction;
 *   - a 32-bit instruction whose upper half is in the next region runs;
 *     each run is checked twice so that a decode-cache hit gives the same
 *     result as the first fetch.
 * The last line is "SELFTEST PASS" or "SELFTEST FAIL <count>".
 */
#include <stdint.h>

#define SERIAL (*(volatile uint8_t *)0x10000000UL)

#define MSTATUS_MPP_MACHINE 0x1800U

#define CAUSE_FETCH_ACCESS_FAULT 1U
#define CAUSE_BREAKPOINT 3U

#define REGION_ALONE_END 0x40101000UL
#define REGION_FOLLOWED_END 0x40201000UL

#define EBREAK 0x00100073U
#define C_EBREAK 0x9002U
#define ADDI_LOWER_HALF 0x0013U	/* lower half of addi x0, x0, 0 */

static void test_putc(char c)
{
	SERIAL = (uint8_t)c;
}

#include "../common/test_report.h"

extern uint64_t run_until_trap(uint64_t entry, uint64_t mpp_bits);
extern volatile uint64_t trap_epc;

static int runs_until(uint64_t address, uint64_t expected_cause)
{
	uint64_t cause = run_until_trap(address, MSTATUS_MPP_MACHINE);

	return cause == expected_cause && trap_epc == address;
}

static void check_inside(uint64_t end)
{
	volatile uint32_t *last_word = (volatile uint32_t *)(uintptr_t)(end - 4U);
	volatile uint16_t *last_half = (volatile uint16_t *)(uintptr_t)(end - 2U);

	*last_word = EBREAK;
	test_check("32-bit instruction ending at the region end runs",
		runs_until(end - 4U, CAUSE_BREAKPOINT));
	*last_half = C_EBREAK;
	test_check("16-bit instruction at the region end runs",
		runs_until(end - 2U, CAUSE_BREAKPOINT));
}

static void check_crossing_into_nothing(uint64_t end)
{
	volatile uint16_t *last_half = (volatile uint16_t *)(uintptr_t)(end - 2U);

	*last_half = ADDI_LOWER_HALF;
	test_check("32-bit instruction crossing into unmapped memory faults",
		runs_until(end - 2U, CAUSE_FETCH_ACCESS_FAULT));
	test_check("same fault on the second run",
		runs_until(end - 2U, CAUSE_FETCH_ACCESS_FAULT));
}

static void check_crossing_into_next_region(uint64_t end)
{
	volatile uint16_t *last_half = (volatile uint16_t *)(uintptr_t)(end - 2U);
	volatile uint16_t *next_region = (volatile uint16_t *)(uintptr_t)end;

	/* addi x0, x0, 0 split over the two regions, then c.ebreak. */
	*last_half = ADDI_LOWER_HALF;
	next_region[0] = 0x0000U;
	next_region[1] = C_EBREAK;
	test_check("32-bit instruction crossing into the next region runs",
		run_until_trap(end - 2U, MSTATUS_MPP_MACHINE) == CAUSE_BREAKPOINT
		&& trap_epc == end + 2U);
	test_check("same result on the second run",
		run_until_trap(end - 2U, MSTATUS_MPP_MACHINE) == CAUSE_BREAKPOINT
		&& trap_epc == end + 2U);
}

int main(void)
{
	check_inside(REGION_ALONE_END);
	check_crossing_into_nothing(REGION_ALONE_END);
	check_inside(REGION_FOLLOWED_END);
	check_crossing_into_next_region(REGION_FOLLOWED_END);
	test_report_finish();
	return 0;
}
