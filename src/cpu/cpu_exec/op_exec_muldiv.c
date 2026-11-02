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

int op_exec_mul(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 result;

	result = core->reg.x[decoded->rs1] * core->reg.x[decoded->rs2];
	return write_result(core, decoded, result);
}

int op_exec_mulh(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	__int128 left = (sint64)core->reg.x[decoded->rs1];
	__int128 right = (sint64)core->reg.x[decoded->rs2];
	uint64 result = (uint64)((left * right) >> 64);

	return write_result(core, decoded, result);
}

int op_exec_mulhsu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	__int128 left = (sint64)core->reg.x[decoded->rs1];
	unsigned __int128 right = core->reg.x[decoded->rs2];
	uint64 result = (uint64)((left * right) >> 64);

	return write_result(core, decoded, result);
}

int op_exec_mulhu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	unsigned __int128 left = core->reg.x[decoded->rs1];
	unsigned __int128 right = core->reg.x[decoded->rs2];
	uint64 result = (uint64)((left * right) >> 64);

	return write_result(core, decoded, result);
}

int op_exec_div(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 dividend = core->reg.x[decoded->rs1];
	uint64 divisor = core->reg.x[decoded->rs2];
	uint64 result;

	/* Division by zero returns all ones; signed overflow returns dividend. */
	if (divisor == 0U) {
		result = ~0ULL;
	}
	else if ((dividend == 0x8000000000000000ULL) &&
		(divisor == ~0ULL)) {
		result = dividend;
	}
	else {
		result = (uint64)((sint64)dividend / (sint64)divisor);
	}
	return write_result(core, decoded, result);
}

int op_exec_divu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 dividend = core->reg.x[decoded->rs1];
	uint64 divisor = core->reg.x[decoded->rs2];
	uint64 result = ~0ULL;

	if (divisor != 0U) {
		result = dividend / divisor;
	}
	return write_result(core, decoded, result);
}

int op_exec_rem(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 dividend = core->reg.x[decoded->rs1];
	uint64 divisor = core->reg.x[decoded->rs2];
	uint64 result;

	/* A zero divisor returns the dividend; signed overflow returns zero. */
	if (divisor == 0U) {
		result = dividend;
	}
	else if ((dividend == 0x8000000000000000ULL) &&
		(divisor == ~0ULL)) {
		result = 0U;
	}
	else {
		result = (uint64)((sint64)dividend % (sint64)divisor);
	}
	return write_result(core, decoded, result);
}

int op_exec_remu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 dividend = core->reg.x[decoded->rs1];
	uint64 divisor = core->reg.x[decoded->rs2];
	uint64 result = dividend;

	if (divisor != 0U) {
		result = dividend % divisor;
	}
	return write_result(core, decoded, result);
}

int op_exec_mulw(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 result;

	result = (uint32)core->reg.x[decoded->rs1];
	result *= (uint32)core->reg.x[decoded->rs2];
	return write_result(core, decoded, op_exec_sign_extend_word(result));
}

static uint32 divide_word(uint32 dividend, uint32 divisor, std_bool is_signed)
{
	if (divisor == 0U) {
		return ~0U;
	}
	if (is_signed == FALSE) {
		return dividend / divisor;
	}
	if ((dividend == 0x80000000U) && (divisor == ~0U)) {
		return dividend;
	}
	return (uint32)((sint32)dividend / (sint32)divisor);
}

static uint32 remainder_word(
	uint32 dividend,
	uint32 divisor,
	std_bool is_signed)
{
	if (divisor == 0U) {
		return dividend;
	}
	if (is_signed == FALSE) {
		return dividend % divisor;
	}
	if ((dividend == 0x80000000U) && (divisor == ~0U)) {
		return 0U;
	}
	return (uint32)((sint32)dividend % (sint32)divisor);
}

#define WORD_DIVIDE(name, helper, signed_value) \
	int op_exec_##name( \
		TargetCoreType *core, \
		const RiscvDecodedInsn *decoded) \
	{ \
		uint32 left = (uint32)core->reg.x[decoded->rs1]; \
		uint32 right = (uint32)core->reg.x[decoded->rs2]; \
		uint32 result = helper(left, right, signed_value); \
		return write_result( \
			core, decoded, op_exec_sign_extend_word(result)); \
	}

/* W-form division follows the same zero and overflow rules, then sign-extends. */
WORD_DIVIDE(divw, divide_word, TRUE)
WORD_DIVIDE(divuw, divide_word, FALSE)
WORD_DIVIDE(remw, remainder_word, TRUE)
WORD_DIVIDE(remuw, remainder_word, FALSE)
