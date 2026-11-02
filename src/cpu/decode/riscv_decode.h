#ifndef ATHRILL_RISCV_DECODE_H
#define ATHRILL_RISCV_DECODE_H
#include "target_cpu.h"
typedef int (*RiscvExecutor)(TargetCoreType *, uint32);
typedef struct { uint32 mask, match; RiscvExecutor execute; } RiscvDispatchEntry;
extern const RiscvDispatchEntry riscv_dispatch_table[];
extern const uint32 riscv_dispatch_table_count;
#endif
