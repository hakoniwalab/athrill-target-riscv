#ifndef ATHRILL_RISCV_CPU_CONFIG_H
#define ATHRILL_RISCV_CPU_CONFIG_H
#include "std_types.h"
#define CPU_CONFIG_CORE_NUM 1
#define CPU_CONFIG_CORE_ID_0 0
#define CPU_CONFIG_MEMORY_PROTECTION_ENABLE
#ifndef CPU_CONFIG_ERBIUM_COMPAT
#define CPU_CONFIG_ERBIUM_COMPAT 1
#endif
struct TargetCoreTypeTag;
void riscv_trace_store(
	struct TargetCoreTypeTag *core,
	uint64 address,
	uint8 size,
	uint64 value);
#endif
