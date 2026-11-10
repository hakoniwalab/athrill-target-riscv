#ifndef ATHRILL_RISCV_INTC_H
#define ATHRILL_RISCV_INTC_H

#include "cpu.h"
#include "mpu_types.h"

/* PLIC source numbers. Source zero is reserved by the PLIC architecture. */
#define INTC_PLIC_SOURCE_MIN 1U
#define INTC_PLIC_SOURCE_MAX 6U

extern uint8 intc_plic_data[0x200008];
extern MpuAddressRegionOperationType intc_plic_ops;

void intc_reset(void);
void intc_set_local_interrupt(CoreIdType core_id, uint32 cause, std_bool level);
std_bool intc_take_interrupt(TargetCoreType *core, uint32 *cause);

/*
 * TRUE when intc_take_interrupt() would take an interrupt now: one is
 * pending and enabled (mip & mie), and the core is not in Machine mode
 * with mstatus.MIE clear. Pending but globally disabled (an interrupt
 * handler that has not yet claimed its source, a critical section) is
 * FALSE: the instructions run as if nothing were pending.
 */
static inline std_bool intc_interrupt_takeable(const TargetCoreType *core)
{
	if ((core->reg.csr.mip & core->reg.csr.mie) == 0U) {
		return FALSE;
	}
	if ((core->reg.privilege == RISCV_PRIV_M) && ((core->reg.csr.mstatus & (1ULL << 3U)) == 0U)) {
		return FALSE;
	}
	return TRUE;
}
void intc_plic_set_source(uint32 source, std_bool level);
/* The level a device drives on a PLIC source (the Devicetree models' irq_is_active). */
std_bool intc_plic_source_asserted(uint32 source);

#endif
