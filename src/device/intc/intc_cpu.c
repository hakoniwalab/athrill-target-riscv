#include "intc.h"

void intc_set_local_interrupt(CoreIdType core_id, uint32 cause, std_bool level)
{
	TargetCoreType *core;
	uint64 mask;

	if ((core_id >= CPU_CONFIG_CORE_NUM) || (cause >= 64U)) {
		return;
	}
	core = &virtual_cpu.cores[core_id].core;
	mask = 1ULL << cause;
	if (level) {
		core->reg.csr.mip |= mask;
	} else {
		core->reg.csr.mip &= ~mask;
	}
	if ((core->reg.csr.mip & core->reg.csr.mie) != 0U) {
		core->is_halt = FALSE;
	}
}

void intc_cpu_trigger_interrupt(CoreIdType core_id, int intno)
{
	if (intno >= 0) {
		intc_set_local_interrupt(core_id, (uint32)intno, TRUE);
	}
}

std_bool intc_take_interrupt(TargetCoreType *core, uint32 *cause)
{
	uint64 pending = core->reg.csr.mip & core->reg.csr.mie;

	if (pending == 0U) {
		return FALSE;
	}
	if ((core->reg.privilege == RISCV_PRIV_M) &&
	    ((core->reg.csr.mstatus & (1ULL << 3U)) == 0U)) {
		return FALSE;
	}
	if ((pending & (1ULL << 11U)) != 0U) {
		*cause = 11U;
	} else if ((pending & (1ULL << 7U)) != 0U) {
		*cause = 7U;
	} else {
		*cause = 3U;
	}
	return TRUE;
}
