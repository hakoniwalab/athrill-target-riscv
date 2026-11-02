#ifndef ATHRILL_RISCV_DECODE_H
#define ATHRILL_RISCV_DECODE_H
#include "target_cpu.h"
typedef int (*RiscvExecutor)(TargetCoreType *, uint32);
typedef struct { uint32 mask, match; RiscvExecutor execute; const char *name; } RiscvDispatchEntry;
enum { RISCV_EXEC_TRAP = 0, RISCV_EXEC_RETIRED = 1 };
void riscv_decode_init(void);
const RiscvDispatchEntry *riscv_decode(uint32 instruction);
#endif
