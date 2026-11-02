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
void intc_plic_set_source(uint32 source, std_bool level);

#endif
