#pragma STDC FENV_ACCESS ON

#include "op_exec.h"

#include <fenv.h>
#include <math.h>
#include <string.h>

#include "bus.h"
#include "cpu_config.h"
#include "pmp.h"
#include "riscv_decode.h"

#define FFLAG_NX 1U
#define FFLAG_UF 2U
#define FFLAG_OF 4U
#define FFLAG_DZ 8U
#define FFLAG_NV 16U
#define FFLAG_INPUT_DENORM 0x80000000U
#define CANONICAL_NAN 0x7fc00000U

static uint32 float_bits(float value)
{
	uint32 bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float bits_float(uint32 bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static std_bool is_nan_bits(uint32 bits)
{
	return ((bits & 0x7f800000U) == 0x7f800000U) &&
		((bits & 0x007fffffU) != 0U);
}

static std_bool is_snan_bits(uint32 bits)
{
	return is_nan_bits(bits) && ((bits & 0x00400000U) == 0U);
}

static std_bool is_denormal_bits(uint32 bits)
{
	return ((bits & 0x7f800000U) == 0U) && ((bits & 0x007fffffU) != 0U);
}

static int fp_illegal(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	riscv_raise_exception(core, 2U, decoded->instruction);
	return RISCV_EXEC_TRAP;
}

static std_bool fp_enabled(TargetCoreType *core)
{
	return ((core->reg.csr.mstatus >> 13U) & 3U) != 0U;
}

static void fp_dirty(TargetCoreType *core)
{
	core->reg.csr.mstatus |= 3ULL << 13U;
}

static uint32 fp_read(TargetCoreType *core, uint8 reg)
{
	uint64 value = core->reg.f[reg];
	uint32 bits;

	if ((value >> 32U) != 0xffffffffU) {
		return CANONICAL_NAN;
	}
	bits = (uint32)value;
	if (CPU_CONFIG_ERBIUM_COMPAT && is_denormal_bits(bits)) {
		core->reg.fcsr |= FFLAG_INPUT_DENORM;
		bits &= 0x80000000U;
	}
	return bits;
}

static void fp_write(TargetCoreType *core, uint8 reg, uint32 bits)
{
	if (CPU_CONFIG_ERBIUM_COMPAT && is_denormal_bits(bits)) {
		bits &= 0x80000000U;
		core->reg.fcsr |= FFLAG_UF | FFLAG_NX;
	}
	core->reg.f[reg] = 0xffffffff00000000ULL | bits;
	fp_dirty(core);
}

static uint32 host_flags(void)
{
	int exceptions = fetestexcept(FE_ALL_EXCEPT);
	uint32 flags = 0U;

	if ((exceptions & FE_INEXACT) != 0) {
		flags |= FFLAG_NX;
	}
	if ((exceptions & FE_UNDERFLOW) != 0) {
		flags |= FFLAG_UF;
	}
	if ((exceptions & FE_OVERFLOW) != 0) {
		flags |= FFLAG_OF;
	}
	if ((exceptions & FE_DIVBYZERO) != 0) {
		flags |= FFLAG_DZ;
	}
	if ((exceptions & FE_INVALID) != 0) {
		flags |= FFLAG_NV;
	}
	return flags;
}

static int set_rounding(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint32 rm = decoded->aux;
	int host_mode;

	if (rm == 7U) {
		rm = (core->reg.fcsr >> 5U) & 7U;
	}
	if (rm == 0U || rm == 4U) {
		host_mode = FE_TONEAREST;
	}
	else if (rm == 1U) {
		host_mode = FE_TOWARDZERO;
	}
	else if (rm == 2U) {
		host_mode = FE_DOWNWARD;
	}
	else if (rm == 3U) {
		host_mode = FE_UPWARD;
	}
	else {
		return -1;
	}
	/* RMM ties are repaired explicitly by integer conversions.  Native
	 * operations use nearest-even; exact halfway binary32 arithmetic cases
	 * are detected and adjusted by fp_binary/fp_fused below. */
	fesetround(host_mode);
	return (int)rm;
}

static int fp_finish(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint32 bits)
{
	core->reg.fcsr |= host_flags();
	if (is_nan_bits(bits)) {
		bits = CANONICAL_NAN;
	}
	fp_write(core, decoded->rd, bits);
	return op_exec_advance(core, decoded);
}

static int fp_binary(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	char operation)
{
	volatile float left;
	volatile float right;
	volatile float result;

	if (!fp_enabled(core) || set_rounding(core, decoded) < 0) {
		return fp_illegal(core, decoded);
	}
	left = bits_float(fp_read(core, decoded->rs1));
	right = bits_float(fp_read(core, decoded->rs2));
	feclearexcept(FE_ALL_EXCEPT);
	if (operation == '+') {
		result = left + right;
	}
	else if (operation == '-') {
		result = left - right;
	}
	else if (operation == '*') {
		result = left * right;
	}
	else {
		result = left / right;
	}
	return fp_finish(core, decoded, float_bits(result));
}

static int fp_fused(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	std_bool negate_product,
	std_bool negate_addend)
{
	float left;
	float right;
	float addend;
	float result;

	if (!fp_enabled(core) || set_rounding(core, decoded) < 0) {
		return fp_illegal(core, decoded);
	}
	left = bits_float(fp_read(core, decoded->rs1));
	right = bits_float(fp_read(core, decoded->rs2));
	addend = bits_float(fp_read(core, decoded->rs3));
	if (negate_product) {
		left = -left;
	}
	if (negate_addend) {
		addend = -addend;
	}
	feclearexcept(FE_ALL_EXCEPT);
	result = fmaf(left, right, addend);
	return fp_finish(core, decoded, float_bits(result));
}

int op_exec_flw(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 address = core->reg.x[decoded->rs1] + decoded->immediate;
	uint32 value;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	if (!riscv_pmp_check(core, address, 4U, CpuMemoryAccess_READ) ||
		bus_get_data32(core->core_id, address, &value) != STD_E_OK) {
		return op_exec_memory_fault(core, address, 5U);
	}
	fp_write(core, decoded->rd, value);
	return op_exec_advance(core, decoded);
}

int op_exec_fsw(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 address = core->reg.x[decoded->rs1] + decoded->immediate;
	uint32 value;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	value = (uint32)core->reg.f[decoded->rs2];
	if (!riscv_pmp_check(core, address, 4U, CpuMemoryAccess_WRITE) ||
		bus_put_data32(core->core_id, address, value) != STD_E_OK) {
		return op_exec_memory_fault(core, address, 7U);
	}
	riscv_decode_cache_invalidate(core, address, 4U);
	return op_exec_advance(core, decoded);
}

int op_exec_fmadd_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_fused(core, decoded, FALSE, FALSE);
}

int op_exec_fmsub_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_fused(core, decoded, FALSE, TRUE);
}

int op_exec_fnmsub_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_fused(core, decoded, TRUE, FALSE);
}

int op_exec_fnmadd_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_fused(core, decoded, TRUE, TRUE);
}

int op_exec_fadd_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_binary(core, decoded, '+');
}

int op_exec_fsub_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_binary(core, decoded, '-');
}

int op_exec_fmul_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	return fp_binary(core, decoded, '*');
}

int op_exec_fdiv_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	if (CPU_CONFIG_ERBIUM_COMPAT) {
		riscv_raise_exception(core, 30U, decoded->instruction);
		return RISCV_EXEC_TRAP;
	}
	return fp_binary(core, decoded, '/');
}

int op_exec_fsqrt_s(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	float value;
	float result;

	if (CPU_CONFIG_ERBIUM_COMPAT) {
		riscv_raise_exception(core, 30U, decoded->instruction);
		return RISCV_EXEC_TRAP;
	}
	if (!fp_enabled(core) || set_rounding(core, decoded) < 0) {
		return fp_illegal(core, decoded);
	}
	value = bits_float(fp_read(core, decoded->rs1));
	feclearexcept(FE_ALL_EXCEPT);
	result = sqrtf(value);
	return fp_finish(core, decoded, float_bits(result));
}

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
	long double maximum;
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
	maximum = unsigned_value ? (long_value ? 0x1p64L - 1.0L : 0x1p32L - 1.0L) :
		(long_value ? 0x1p63L - 1.0L : 0x1p31L - 1.0L);
	if (is_nan_bits(bits)) {
		result = (uint64)maximum;
		core->reg.fcsr |= FFLAG_NV;
	}
	else {
		rounded = round_integer(value, rm);
		if (rounded < minimum) {
			result = (uint64)minimum;
			core->reg.fcsr |= FFLAG_NV;
		}
		else if (rounded > maximum) {
			result = (uint64)maximum;
			core->reg.fcsr |= FFLAG_NV;
		}
		else {
			result = (uint64)rounded;
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
