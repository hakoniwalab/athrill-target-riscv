#pragma STDC FENV_ACCESS ON

#include "op_exec_fpu_internal.h"

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
	long double exact;
	int rounding;

	rounding = set_rounding(core, decoded);
	if (!fp_enabled(core) || rounding < 0) {
		return fp_illegal(core, decoded);
	}
	left = bits_float(fp_read(core, decoded->rs1));
	right = bits_float(fp_read(core, decoded->rs2));
	feclearexcept(FE_ALL_EXCEPT);
	if (operation == '+') {
		result = left + right;
		exact = (long double)left + (long double)right;
	}
	else if (operation == '-') {
		result = left - right;
		exact = (long double)left - (long double)right;
	}
	else if (operation == '*') {
		result = left * right;
		exact = (long double)left * (long double)right;
	}
	else {
		result = left / right;
		exact = (long double)left / (long double)right;
	}
	return fp_finish(core, decoded, rounding == 4 ?
		rmm_adjust(exact, float_bits(result)) : float_bits(result));
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
	long double exact;
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
	feclearexcept(FE_ALL_EXCEPT);
	result = fmaf(left, right, addend);
	exact = fmal((long double)left, (long double)right, (long double)addend);
	return fp_finish(core, decoded, rounding == 4 ?
		rmm_adjust(exact, float_bits(result)) : float_bits(result));
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
	feclearexcept(FE_ALL_EXCEPT);
	result = sqrtf(value);
	return fp_finish(core, decoded, rounding == 4 ?
		rmm_adjust(sqrtl((long double)value), float_bits(result)) : float_bits(result));
}
