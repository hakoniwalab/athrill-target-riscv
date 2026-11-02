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

int op_exec_slli(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 shift = (uint32)decoded->immediate & 63U;
	uint64 result = core->reg.x[decoded->rs1] << shift;

	return write_result(core, decoded, result);
}

int op_exec_srli(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 shift = (uint32)decoded->immediate & 63U;
	uint64 result = core->reg.x[decoded->rs1] >> shift;

	return write_result(core, decoded, result);
}

int op_exec_srai(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 shift = (uint32)decoded->immediate & 63U;
	sint64 source = (sint64)core->reg.x[decoded->rs1];

	return write_result(core, decoded, (uint64)(source >> shift));
}

int op_exec_sll(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 shift = (uint32)core->reg.x[decoded->rs2] & 63U;
	uint64 result = core->reg.x[decoded->rs1] << shift;

	return write_result(core, decoded, result);
}

int op_exec_srl(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 shift = (uint32)core->reg.x[decoded->rs2] & 63U;
	uint64 result = core->reg.x[decoded->rs1] >> shift;

	return write_result(core, decoded, result);
}

int op_exec_sra(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 shift = (uint32)core->reg.x[decoded->rs2] & 63U;
	sint64 source = (sint64)core->reg.x[decoded->rs1];

	return write_result(core, decoded, (uint64)(source >> shift));
}

static uint32 immediate_word_shift(const RiscvDecodedInsn *decoded)
{
	return (uint32)decoded->immediate & 31U;
}

static uint32 register_word_shift(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded)
{
	return (uint32)core->reg.x[decoded->rs2] & 31U;
}

#define WORD_SHIFT_LEFT(name, shift_expression) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		uint32 source = (uint32)core->reg.x[decoded->rs1]; \
		uint32 result = source << (shift_expression); \
		return write_result( \
			core, decoded, op_exec_sign_extend_word(result)); \
	}

#define WORD_SHIFT_RIGHT(name, shift_expression) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		uint32 source = (uint32)core->reg.x[decoded->rs1]; \
		uint32 result = source >> (shift_expression); \
		return write_result( \
			core, decoded, op_exec_sign_extend_word(result)); \
	}

#define WORD_SHIFT_ARITHMETIC(name, shift_expression) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		sint32 source = (sint32)core->reg.x[decoded->rs1]; \
		uint32 result = (uint32)(source >> (shift_expression)); \
		return write_result( \
			core, decoded, op_exec_sign_extend_word(result)); \
	}

/* All W-form results are sign-extended to XLEN. */
WORD_SHIFT_LEFT(slliw, immediate_word_shift(decoded))
WORD_SHIFT_RIGHT(srliw, immediate_word_shift(decoded))
WORD_SHIFT_ARITHMETIC(sraiw, immediate_word_shift(decoded))
WORD_SHIFT_LEFT(sllw, register_word_shift(core, decoded))
WORD_SHIFT_RIGHT(srlw, register_word_shift(core, decoded))
WORD_SHIFT_ARITHMETIC(sraw, register_word_shift(core, decoded))
