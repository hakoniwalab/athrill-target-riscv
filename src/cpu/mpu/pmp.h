#ifndef ATHRILL_RISCV_PMP_H
#define ATHRILL_RISCV_PMP_H
#include "target_cpu.h"
std_bool riscv_pmp_check(const TargetCoreType *core, uint64 addr, uint32 size, CpuMemoryAccessType access);
void riscv_raise_exception(TargetCoreType *core, uint64 cause, uint64 tval);
#endif
