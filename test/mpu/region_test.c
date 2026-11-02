/*
 * Memory region lookup next to a region that starts and ends inside a
 * 1 KiB page.
 *
 * After a load inside the region, loads just before it, just after it and
 * across its end (same page each time) must not be served from that
 * region: they are load access faults (cause 5) at the load. A load inside
 * the region reaches its ECALL (cause 11).
 * The last line is "SELFTEST PASS" or "SELFTEST FAIL <count>".
 */
#include <stdint.h>

#define SERIAL (*(volatile uint8_t *)0x10000000UL)

#define MSTATUS_MPP_MACHINE 0x1800U
#define CAUSE_LOAD_ACCESS_FAULT 5U
#define CAUSE_ECALL_FROM_MACHINE 11U

static void test_putc(char c)
{
	SERIAL = (uint8_t)c;
}

#include "../common/test_report.h"

extern uint64_t run_until_trap(uint64_t entry, uint64_t mpp_bits);
extern volatile uint64_t trap_epc;
extern char load_inside_region[];
extern char load_before_region[];
extern char load_before_region_insn[];
extern char load_after_region[];
extern char load_after_region_insn[];
extern char load_across_region_end[];
extern char load_across_region_end_insn[];

static uint64_t run(char *entry)
{
	return run_until_trap((uint64_t)(uintptr_t)entry, MSTATUS_MPP_MACHINE);
}

static int faults_at(char *entry, char *instruction)
{
	return run(entry) == CAUSE_LOAD_ACCESS_FAULT
		&& trap_epc == (uint64_t)(uintptr_t)instruction;
}

int main(void)
{
	test_check("load inside the region",
		run(load_inside_region) == CAUSE_ECALL_FROM_MACHINE);
	test_check("load before the region faults",
		faults_at(load_before_region, load_before_region_insn));
	(void)run(load_inside_region);
	test_check("load after the region faults",
		faults_at(load_after_region, load_after_region_insn));
	(void)run(load_inside_region);
	test_check("load across the region end faults",
		faults_at(load_across_region_end, load_across_region_end_insn));
	test_report_finish();
	return 0;
}
