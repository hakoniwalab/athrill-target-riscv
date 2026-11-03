/*
 * PMP address matching and permissions (RISC-V privileged specification).
 *
 * pmpcfg bytes used: R 0x01, W 0x02, X 0x04, A = TOR 0x08 / NA4 0x10 /
 * NAPOT 0x18, L 0x80. pmpaddr holds the address shifted right by 2.
 */
#include "pmp.h"
#include <stdio.h>
#include <string.h>

#define PMP_TOR_RW 0x0bU
#define PMP_NA4_RW 0x13U
#define PMP_NAPOT_RWX 0x1fU
#define PMP_LOCKED_NAPOT_X 0x9cU

static unsigned failures;

static void check(const char *name, int passed)
{
	printf("%s %s\n", passed ? "PASS" : "FAIL", name);
	if (!passed) {
		failures++;
	}
}

static void start(TargetCoreType *core, RiscvPrivilege privilege)
{
	memset(core, 0, sizeof(*core));
	core->reg.privilege = privilege;
}

static void set_config(TargetCoreType *core, unsigned entry, uint8 config)
{
	core->reg.csr.pmpcfg[entry / 8U] |= (uint64)config << ((entry % 8U) * 8U);
}

static void check_tor(void)
{
	TargetCoreType core;

	start(&core, RISCV_PRIV_U);
	set_config(&core, 0, PMP_TOR_RW);
	core.reg.csr.pmpaddr[0] = 0x1000 >> 2;	/* [0, 0x1000) */
	check("TOR: read inside", riscv_pmp_check(&core, 0x100, 4, CpuMemoryAccess_READ));
	check("TOR: no execute without X", !riscv_pmp_check(&core, 0x100, 4, CpuMemoryAccess_EXEC));
	check("TOR: access across the top fails", !riscv_pmp_check(&core, 0xfff, 2, CpuMemoryAccess_READ));
}

static void check_na4(void)
{
	TargetCoreType core;

	start(&core, RISCV_PRIV_U);
	set_config(&core, 0, PMP_NA4_RW);
	core.reg.csr.pmpaddr[0] = 0x2000 >> 2;	/* [0x2000, 0x2004) */
	check("NA4: write inside", riscv_pmp_check(&core, 0x2000, 4, CpuMemoryAccess_WRITE));
	check("NA4: next word not covered", !riscv_pmp_check(&core, 0x2004, 1, CpuMemoryAccess_READ));
}

static void check_napot_and_lock(void)
{
	TargetCoreType core;

	start(&core, RISCV_PRIV_U);
	set_config(&core, 0, PMP_NAPOT_RWX);
	core.reg.csr.pmpaddr[0] = (0x4000 >> 2) | 1U;	/* 16 bytes at 0x4000 */
	check("NAPOT: execute the whole 16 bytes", riscv_pmp_check(&core, 0x4000, 16, CpuMemoryAccess_EXEC));
	check("NAPOT: User denied outside any entry", !riscv_pmp_check(&core, 0x4010, 1, CpuMemoryAccess_READ));
	core.reg.privilege = RISCV_PRIV_M;
	check("Machine allowed outside any entry", riscv_pmp_check(&core, 0x4010, 1, CpuMemoryAccess_READ));
	core.reg.csr.pmpcfg[0] = PMP_LOCKED_NAPOT_X;
	check("locked entry without R restricts Machine too", !riscv_pmp_check(&core, 0x4000, 1, CpuMemoryAccess_READ));
}

int main(void)
{
	check_tor();
	check_na4();
	check_napot_and_lock();
	printf("%s\n", failures == 0U ? "SELFTEST PASS" : "SELFTEST FAIL");
	return failures == 0U ? 0 : 1;
}
