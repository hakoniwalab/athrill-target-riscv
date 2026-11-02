#include "op_exec.h"

static int write_result(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint64 result)
{
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = result;
	}
	return op_exec_advance(core, decoded);
}

#define LOGIC_IMMEDIATE(name, operation) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		uint64 result; \
		result = core->reg.x[decoded->rs1] operation decoded->immediate; \
		return write_result(core, decoded, result); \
	}

#define LOGIC_REGISTER(name, operation) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		uint64 result; \
		result = core->reg.x[decoded->rs1] operation \
			core->reg.x[decoded->rs2]; \
		return write_result(core, decoded, result); \
	}

LOGIC_IMMEDIATE(xori, ^)
LOGIC_IMMEDIATE(ori, |)
LOGIC_IMMEDIATE(andi, &)
LOGIC_REGISTER(xor, ^)
LOGIC_REGISTER(or, |)
LOGIC_REGISTER(and, &)
