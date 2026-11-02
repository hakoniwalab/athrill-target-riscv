#include "op_exec_fpu_internal.h"

#include "cpu_config.h"

static int fp_sign(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint32 mode)
{
	uint32 left;
	uint32 right;
	uint32 sign;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	left = fp_read(core, decoded->rs1);
	right = fp_read(core, decoded->rs2);
	sign = right & 0x80000000U;
	if (mode == 1U) {
		sign ^= 0x80000000U;
	}
	else if (mode == 2U) {
		sign ^= left & 0x80000000U;
	}
	fp_write(core, decoded->rd, (left & 0x7fffffffU) | sign);
	return op_exec_advance(core, decoded);
}

int op_exec_fsgnj_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_sign(core, decoded, 0U);
}

int op_exec_fsgnjn_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_sign(core, decoded, 1U);
}

int op_exec_fsgnjx_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_sign(core, decoded, 2U);
}

static int fp_minmax(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	std_bool maximum)
{
	uint32 left = fp_read(core, decoded->rs1);
	uint32 right = fp_read(core, decoded->rs2);
	uint32 result;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	if (is_snan_bits(left) || is_snan_bits(right)) {
		core->reg.fcsr |= FFLAG_NV;
	}
	if (is_nan_bits(left) && is_nan_bits(right)) {
		result = CANONICAL_NAN;
	}
	else if (is_nan_bits(left)) {
		result = right;
	}
	else if (is_nan_bits(right)) {
		result = left;
	}
	else if ((left << 1U) == 0U && (right << 1U) == 0U) {
		result = maximum ? (left & right) : (left | right);
	}
	else if (maximum == (bits_float(left) < bits_float(right))) {
		result = right;
	}
	else {
		result = left;
	}
	fp_write(core, decoded->rd, result);
	return op_exec_advance(core, decoded);
}

int op_exec_fmin_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_minmax(core, decoded, FALSE);
}

int op_exec_fmax_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_minmax(core, decoded, TRUE);
}

static int fp_compare(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint32 operation)
{
	uint32 left = fp_read(core, decoded->rs1);
	uint32 right = fp_read(core, decoded->rs2);
	std_bool nan = is_nan_bits(left) || is_nan_bits(right);
	uint64 result = 0U;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	if ((operation != 0U && nan) || is_snan_bits(left) || is_snan_bits(right)) {
		core->reg.fcsr |= FFLAG_NV;
	}
	if (!nan) {
		if (operation == 0U) {
			result = bits_float(left) == bits_float(right);
		}
		else if (operation == 1U) {
			result = bits_float(left) < bits_float(right);
		}
		else {
			result = bits_float(left) <= bits_float(right);
		}
	}
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = result;
	}
	fp_dirty(core);
	return op_exec_advance(core, decoded);
}

int op_exec_feq_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_compare(core, decoded, 0U);
}

int op_exec_flt_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_compare(core, decoded, 1U);
}

int op_exec_fle_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_compare(core, decoded, 2U);
}

int op_exec_fclass_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 bits = fp_read(core, decoded->rs1);
	uint32 exponent = bits & 0x7f800000U;
	uint32 fraction = bits & 0x007fffffU;
	uint32 result;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	if (exponent == 0x7f800000U) {
		if (fraction != 0U) {
			result = is_snan_bits(bits) ? (1U << 8U) : (1U << 9U);
		}
		else {
			result = (bits >> 31U) ? 1U : (1U << 7U);
		}
	}
	else if (exponent == 0U) {
		if (fraction == 0U) {
			result = (bits >> 31U) ? (1U << 3U) : (1U << 4U);
		}
		else {
			result = (bits >> 31U) ? (1U << 2U) : (1U << 5U);
		}
	}
	else {
		result = (bits >> 31U) ? (1U << 1U) : (1U << 6U);
	}
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = result;
	}
	return op_exec_advance(core, decoded);
}
