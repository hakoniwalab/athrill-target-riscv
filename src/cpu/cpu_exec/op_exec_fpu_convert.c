#pragma STDC FENV_ACCESS ON

#include "op_exec_fpu_internal.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#include "cpu_config.h"
#include "pmp.h"

int op_exec_fmv_x_w(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 bits;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	bits = (uint32)core->reg.f[decoded->rs1];
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = op_exec_sign_extend(bits, 32U);
	}
	return op_exec_advance(core, decoded);
}

int op_exec_fmv_w_x(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	fp_write(core, decoded->rd, (uint32)core->reg.x[decoded->rs1]);
	return op_exec_advance(core, decoded);
}

static long double round_integer(long double value, uint32 rm)
{
	long double lower;
	long double fraction;

	if (rm == 1U) {
		return truncl(value);
	}
	if (rm == 2U) {
		return floorl(value);
	}
	if (rm == 3U) {
		return ceill(value);
	}
	if (rm == 0U) {
		return nearbyintl(value);
	}
	lower = floorl(value);
	fraction = value - lower;
	return fraction < 0.5L ? lower : lower + 1.0L;
}

static int fp_to_integer(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	std_bool unsigned_value,
	std_bool long_value)
{
	uint32 bits;
	uint32 rm;
	long double value;
	long double rounded;
	long double minimum;
	long double upper_limit;
	uint64 result;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	if (CPU_CONFIG_ERBIUM_COMPAT && long_value) {
		riscv_raise_exception(core, 30U, decoded->instruction);
		return RISCV_EXEC_TRAP;
	}
	rm = decoded->aux == 7U ? ((core->reg.fcsr >> 5U) & 7U) : decoded->aux;
	if (rm > 4U) {
		return fp_illegal(core, decoded);
	}
	bits = fp_read(core, decoded->rs1);
	value = bits_float(bits);
	minimum = unsigned_value ? 0.0L : (long_value ? -0x1p63L : -0x1p31L);
	upper_limit = unsigned_value ? (long_value ? 0x1p64L : 0x1p32L) :
		(long_value ? 0x1p63L : 0x1p31L);
	if (is_nan_bits(bits)) {
		result = unsigned_value ? (long_value ? UINT64_MAX : UINT32_MAX) :
			(long_value ? INT64_MAX : INT32_MAX);
		core->reg.fcsr |= FFLAG_NV;
	}
	else {
		rounded = round_integer(value, rm);
		if (rounded < minimum) {
			result = unsigned_value ? 0U :
				(long_value ? (uint64)INT64_MIN : (uint64)INT32_MIN);
			core->reg.fcsr |= FFLAG_NV;
		}
		else if (rounded >= upper_limit) {
			result = unsigned_value ? (long_value ? UINT64_MAX : UINT32_MAX) :
				(long_value ? INT64_MAX : INT32_MAX);
			core->reg.fcsr |= FFLAG_NV;
		}
		else {
			result = unsigned_value ? (uint64)rounded : (uint64)(sint64)rounded;
			if (rounded != value) {
				core->reg.fcsr |= FFLAG_NX;
			}
		}
	}
	if (!long_value) {
		result = op_exec_sign_extend(result, 32U);
	}
	if (decoded->rd != 0U) {
		core->reg.x[decoded->rd] = result;
	}
	fp_dirty(core);
	return op_exec_advance(core, decoded);
}

int op_exec_fcvt_w_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_to_integer(core, decoded, FALSE, FALSE);
}

int op_exec_fcvt_wu_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_to_integer(core, decoded, TRUE, FALSE);
}

int op_exec_fcvt_l_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_to_integer(core, decoded, FALSE, TRUE);
}

int op_exec_fcvt_lu_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_to_integer(core, decoded, TRUE, TRUE);
}

static int integer_to_fp(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	std_bool unsigned_value,
	std_bool long_value)
{
	volatile float result;

	if (!fp_enabled(core) || set_rounding(core, decoded) < 0) {
		return fp_illegal(core, decoded);
	}
	if (CPU_CONFIG_ERBIUM_COMPAT && long_value) {
		riscv_raise_exception(core, 30U, decoded->instruction);
		return RISCV_EXEC_TRAP;
	}
	feclearexcept(FE_ALL_EXCEPT);
	if (unsigned_value && long_value) {
		result = (float)core->reg.x[decoded->rs1];
	}
	else if (unsigned_value) {
		result = (float)(uint32)core->reg.x[decoded->rs1];
	}
	else if (long_value) {
		result = (float)(sint64)core->reg.x[decoded->rs1];
	}
	else {
		result = (float)(sint32)core->reg.x[decoded->rs1];
	}
	return fp_finish(core, decoded, float_bits(result));
}

int op_exec_fcvt_s_w(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return integer_to_fp(core, decoded, FALSE, FALSE);
}

int op_exec_fcvt_s_wu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return integer_to_fp(core, decoded, TRUE, FALSE);
}

int op_exec_fcvt_s_l(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return integer_to_fp(core, decoded, FALSE, TRUE);
}

int op_exec_fcvt_s_lu(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return integer_to_fp(core, decoded, TRUE, TRUE);
}
