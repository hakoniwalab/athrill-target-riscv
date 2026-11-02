#pragma STDC FENV_ACCESS ON

#include "op_exec_fpu_internal.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "bus.h"
#include "cpu_config.h"
#include "pmp.h"
#include "riscv_decode.h"

uint32 float_bits(float value)
{
	uint32 bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

float bits_float(uint32 bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

std_bool is_nan_bits(uint32 bits)
{
	return ((bits & 0x7f800000U) == 0x7f800000U) &&
		((bits & 0x007fffffU) != 0U);
}

std_bool is_snan_bits(uint32 bits)
{
	return is_nan_bits(bits) && ((bits & 0x00400000U) == 0U);
}

static std_bool is_denormal_bits(uint32 bits)
{
	return ((bits & 0x7f800000U) == 0U) && ((bits & 0x007fffffU) != 0U);
}

int fp_illegal(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	riscv_raise_exception(core, 2U, decoded->instruction);
	return RISCV_EXEC_TRAP;
}

std_bool fp_enabled(TargetCoreType *core)
{
	return ((core->reg.csr.mstatus >> 13U) & 3U) != 0U;
}

void fp_dirty(TargetCoreType *core)
{
	core->reg.csr.mstatus |= 3ULL << 13U;
}

uint32 fp_read(TargetCoreType *core, uint8 reg)
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

void fp_write(TargetCoreType *core, uint8 reg, uint32 bits)
{
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

int set_rounding(TargetCoreType *core, const RiscvDecodedInsn *decoded)
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

int fp_finish(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	uint32 bits)
{
	core->reg.fcsr |= host_flags();
	if (CPU_CONFIG_ERBIUM_COMPAT && is_denormal_bits(bits)) {
		bits &= 0x80000000U;
		core->reg.fcsr |= FFLAG_UF | FFLAG_NX;
	}
	if (is_nan_bits(bits)) {
		bits = CANONICAL_NAN;
	}
	fp_write(core, decoded->rd, bits);
	return op_exec_advance(core, decoded);
}

uint32 rmm_adjust(long double exact, uint32 rounded_bits)
{
	float rounded = bits_float(rounded_bits);
	float neighbor;
	long double midpoint;

	if (!isfinite(rounded) || isnan((double)exact) || isinf((double)exact) ||
		exact == (long double)rounded) {
		return rounded_bits;
	}
	neighbor = nextafterf(rounded, exact > (long double)rounded ? INFINITY : -INFINITY);
	if (!isfinite(neighbor)) {
		return rounded_bits;
	}
	midpoint = ((long double)rounded + (long double)neighbor) / 2.0L;
	if ((exact == midpoint) && (fabsl((long double)neighbor) > fabsl((long double)rounded))) {
		return float_bits(neighbor);
	}
	return rounded_bits;
}
