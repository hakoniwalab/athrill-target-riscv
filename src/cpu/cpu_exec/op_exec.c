#include "op_exec.h"

int op_exec_advance(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	core->reg.pc += decoded->length;
	return RISCV_EXEC_RETIRED;
}

uint64 op_exec_sign_extend(uint64 value, uint32 width)
{
	uint32 shift = 64U - width;

	return (uint64)((sint64)(value << shift) >> shift);
}

uint64 op_exec_sign_extend_word(uint32 value)
{
	return (uint64)(sint64)(sint32)value;
}

int op_exec_discard(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return op_exec_advance(core, decoded);
}

int op_exec_li(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	/* Writes to x0 are discarded by the architecture. */
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = decoded->immediate;
	}
	return op_exec_advance(core, decoded);
}

int op_exec_mv(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = core->reg.x[decoded->rs1];
	}
	return op_exec_advance(core, decoded);
}
