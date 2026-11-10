#pragma STDC FENV_ACCESS ON

#include "op_exec_fpu_internal.h"
#include "op_exec_memory.h"

#include <fenv.h>
#include <math.h>

#include "bus.h"
#include "cpu_config.h"
#include "pmp.h"
#include "riscv_decode.h"

static int fp_binary(
	TargetCoreType *core,
	const RiscvDecodedInsn *decoded,
	char operation)
{
	volatile float left;
	volatile float right;
	volatile float result;
	double exact;
	int rounding;

	rounding = set_rounding(core, decoded);
	if (!fp_enabled(core) || rounding < 0) {
		return fp_illegal(core, decoded);
	}
	left = bits_float(fp_read(core, decoded->rs1));
	right = bits_float(fp_read(core, decoded->rs2));
	fp_host_clear_flags();
	if (operation == '+') {
		result = left + right;
		exact = (double)left + (double)right;
	}
	else if (operation == '-') {
		result = left - right;
		exact = (double)left - (double)right;
	}
	else if (operation == '*') {
		result = left * right;
		exact = (double)left * (double)right;
	}
	else {
		result = left / right;
		exact = (double)left / (double)right;
	}
	/*
	 * In double, +, - and * of two floats are exact, and / is exact
	 * whenever the quotient is a float midpoint and cannot round onto one
	 * otherwise (53 >= 2 * 24 + 2 bits), so the low part is 0.
	 */
	return fp_finish(core, decoded, rounding == 4 ?
		rmm_adjust(exact, 0.0, float_bits(result)) : float_bits(result));
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
	double product;
	double exact_high;
	double exact_low;
	double addend_part;
	int rounding;

	rounding = set_rounding(core, decoded);
	if (!fp_enabled(core) || rounding < 0) {
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
	fp_host_clear_flags();
	result = fmaf(left, right, addend);
	if (rounding != 4) {
		return fp_finish(core, decoded, float_bits(result));
	}
	/*
	 * The exact left * right + addend may need more than 53 bits, so it is
	 * kept as exact_high + exact_low: the product of two floats is exact in
	 * double, and TwoSum (round to nearest, set for RMM) splits the sum
	 * into its rounded value and the exact rounding error.
	 */
	product = (double)left * (double)right;
	exact_high = product + (double)addend;
	addend_part = exact_high - product;
	exact_low = (product - (exact_high - addend_part)) + ((double)addend - addend_part);
	return fp_finish(core, decoded, rmm_adjust(exact_high, exact_low, float_bits(result)));
}

int op_exec_flw(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 address = core->reg.x[decoded->rs1] + decoded->immediate;
	uint32 value;
	uint8 *pointer;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	pointer = op_exec_memory_direct(core, address, 4U, CpuMemoryAccess_READ);
	if (pointer == NULL) {
		if (!riscv_pmp_check(core, address, 4U, CpuMemoryAccess_READ)) {
			return op_exec_memory_fault(core, address, 5U);
		}
		pointer = op_exec_memory_pointer(core, address, 4U, FALSE);
	}
	if (pointer != NULL) {
		value = (uint32)op_exec_memory_read(pointer, 4U);
	}
	else if (bus_get_data32(core->core_id, address, &value) != STD_E_OK) {
		return op_exec_memory_fault(core, address, 5U);
	}
	fp_write(core, decoded->rd, value);
	return op_exec_advance(core, decoded);
}

int op_exec_fsw(TargetCoreType *core, const RiscvDecodedInsn *decoded)
{
	uint64 address = core->reg.x[decoded->rs1] + decoded->immediate;
	uint32 value;
	uint8 *pointer;

	if (!fp_enabled(core)) {
		return fp_illegal(core, decoded);
	}
	value = (uint32)core->reg.f[decoded->rs2];
	pointer = op_exec_memory_direct(core, address, 4U, CpuMemoryAccess_WRITE);
	if (pointer == NULL) {
		if (!riscv_pmp_check(core, address, 4U, CpuMemoryAccess_WRITE)) {
			return op_exec_memory_fault(core, address, 7U);
		}
		pointer = op_exec_memory_pointer(core, address, 4U, TRUE);
	}
	if (pointer != NULL) {
		op_exec_memory_write(pointer, 4U, value);
	}
	else if (bus_put_data32(core->core_id, address, value) != STD_E_OK) {
		return op_exec_memory_fault(core, address, 7U);
	}
	riscv_trace_store(core, address, 4U, value);
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
	int rounding;

	if (CPU_CONFIG_ERBIUM_COMPAT) {
		riscv_raise_exception(core, 30U, decoded->instruction);
		return RISCV_EXEC_TRAP;
	}
	rounding = set_rounding(core, decoded);
	if (!fp_enabled(core) || rounding < 0) {
		return fp_illegal(core, decoded);
	}
	value = bits_float(fp_read(core, decoded->rs1));
	fp_host_clear_flags();
	result = sqrtf(value);
	return fp_finish(core, decoded, rounding == 4 ?
		rmm_adjust(sqrt((double)value), 0.0, float_bits(result)) : float_bits(result));
}
