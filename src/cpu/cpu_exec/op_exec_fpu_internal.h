#ifndef ATHRILL_RISCV_OP_EXEC_FPU_INTERNAL_H
#define ATHRILL_RISCV_OP_EXEC_FPU_INTERNAL_H

#include "op_exec.h"

#define FFLAG_NX 1U
#define FFLAG_UF 2U
#define FFLAG_OF 4U
#define FFLAG_DZ 8U
#define FFLAG_NV 16U
#define FFLAG_INPUT_DENORM 0x80000000U
#define CANONICAL_NAN 0x7fc00000U

uint32 float_bits(float value);
float bits_float(uint32 bits);
std_bool is_nan_bits(uint32 bits);
std_bool is_snan_bits(uint32 bits);
int fp_illegal(TargetCoreType *core, const RiscvDecodedInsn *decoded);
std_bool fp_enabled(TargetCoreType *core);
void fp_dirty(TargetCoreType *core);
uint32 fp_read(TargetCoreType *core, uint8 reg);
void fp_write(TargetCoreType *core, uint8 reg, uint32 bits);
int set_rounding(TargetCoreType *core, const RiscvDecodedInsn *decoded);
int fp_finish(TargetCoreType *core, const RiscvDecodedInsn *decoded, uint32 bits);
uint32 rmm_adjust(long double exact, uint32 rounded_bits);

#endif
