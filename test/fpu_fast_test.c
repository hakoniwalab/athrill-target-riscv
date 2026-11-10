/* Differential test for the flag-free binary32 arithmetic paths. */
#include "cpu.h"
#include "std_device_ops.h"
#include "std_cpu_ops.h"
#include "athrill_idle_spin_halt.h"
#include "op_exec.h"
#include "op_exec_fpu_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "device_tree.h"

#define HOST_MEMORY_SIZE 256U
#include "common/host_memory_stub.h"
#define RANDOM_SEED 0x243f6a8885a308d3ULL
#if CPU_CONFIG_ERBIUM_COMPAT
#define RANDOM_INPUTS 0U
#define EDGE_INPUTS 0U
#else
#define RANDOM_INPUTS 2000000U
#define EDGE_INPUTS 200000U
#endif
#define INITIAL_FCSR FFLAG_DZ
#define INITIAL_MSTATUS (1ULL << 13U)

std_bool riscv_device_tree_build(void) { return FALSE; }
void riscv_device_tree_tick(DeviceClockType *clock) { (void)clock; }
#ifdef EXDEV_ENABLE
void device_supply_clock_exdev(DeviceClockType *clock) { (void)clock; }
void device_init_athrill_exdev(void) {}
#endif
Std_ReturnType cpuemu_get_devcfg_value(const char *key, uint32 *value)
{ (void)key; (void)value; return STD_E_INVALID; }
Std_ReturnType cpuemu_get_devcfg_string(const char *key, char **value)
{ (void)key; (void)value; return STD_E_INVALID; }
uint64 cpuemu_get_clock(void) { return 0U; }
int symbol_get_func(char *name, uint32 length, AthrillAddrType *address, uint32 *size)
{ (void)name; (void)length; (void)address; (void)size; return -1; }

extern void riscv_fpu_fast_test_set_disabled(std_bool disabled);
extern uint64 riscv_fpu_fast_test_hits(void);
typedef int (*FpuOperation)(TargetCoreType *, const RiscvDecodedInsn *);
typedef struct { const char *name; FpuOperation execute; } Operation;
typedef struct { uint32 left, right, addend; const char *name; } Input;
typedef struct {
	uint64 destination, pc, mstatus, fast_hits;
	uint32 fcsr;
	int retired;
} Result;

static const Operation operations[] = {
	{ "fadd.s", op_exec_fadd_s }, { "fsub.s", op_exec_fsub_s },
	{ "fmul.s", op_exec_fmul_s }, { "fdiv.s", op_exec_fdiv_s },
	{ "fmadd.s", op_exec_fmadd_s }, { "fmsub.s", op_exec_fmsub_s },
	{ "fnmsub.s", op_exec_fnmsub_s }, { "fnmadd.s", op_exec_fnmadd_s }
};

static const Input common_inputs[] = {
	{ 0x00000000U, 0x00000000U, 0x00000000U, "+0, +0, +0" },
	{ 0x80000000U, 0x80000000U, 0x80000000U, "-0, -0, -0" },
	{ 0x00000000U, 0x80000000U, 0x00000000U, "+0, -0, +0" },
	{ 0x00800000U, 0x3f800000U, 0x00000000U, "smallest normal" },
	{ 0x7f7fffffU, 0x3f800000U, 0x00000000U, "largest normal" },
	{ 0x00000001U, 0x3f800000U, 0x00000001U, "smallest subnormal inputs" },
	{ 0x007fffffU, 0x3f800000U, 0x807fffffU, "largest subnormal inputs" },
	{ 0x7f800000U, 0x3f800000U, 0x3f800000U, "+infinity" },
	{ 0xff800000U, 0x3f800000U, 0xbf800000U, "-infinity" },
	{ 0x7fc12345U, 0x3f800000U, 0x3f800000U, "quiet NaN" },
	{ 0x7f812345U, 0x3f800000U, 0x3f800000U, "signalling NaN" },
	{ 0x3f800000U, 0x7fc54321U, 0x3f800000U, "right quiet NaN" },
	{ 0x3f800000U, 0x7f854321U, 0x3f800000U, "right signalling NaN" },
	{ 0x3f800000U, 0x3f800000U, 0x7f800001U, "addend signalling NaN" },
	{ 0x7f800000U, 0x00000000U, 0x3f800000U, "infinity times zero" },
	{ 0x3f800000U, 0x00000000U, 0x00000000U, "division by zero" },
	{ 0x00000000U, 0x00000000U, 0x00000000U, "zero divided by zero" }
};
static const Input add_inputs[] = {
	{ 0x3f800000U, 0x3f800000U, 0U, "exact 1 + 1" },
	{ 0x3f800000U, 0x33800000U, 0U, "halfway at 1, tie to even" },
	{ 0x3f800001U, 0x33800000U, 0U, "halfway at odd significand" },
	{ 0x3f800000U, 0x0d800000U, 0U, "far apart exponents (TwoSum)" },
	{ 0x00800000U, 0x00800000U, 0U, "inside lower bound" },
	{ 0x00800000U, 0x80000001U, 0U, "just outside lower bound" },
	{ 0x7effffffU, 0x73000000U, 0U, "round up to next binade" },
	{ 0x7e800000U, 0x7e800000U, 0U, "exact upper bound 2^127" },
	{ 0x7e800001U, 0x7e800001U, 0U, "just outside upper bound" },
	{ 0x7f7fffffU, 0x73000000U, 0U, "overflow midpoint below 2^128" },
	{ 0x3f800000U, 0xbf800000U, 0U, "exact cancellation" },
	{ 0x80000000U, 0x80000000U, 0U, "negative zero sum" }
};
static const Input mul_inputs[] = {
	{ 0x3fc00000U, 0x40000000U, 0U, "exact product" },
	{ 0x00800000U, 0x3f800000U, 0U, "exact lower bound 2^-126" },
	{ 0x00800000U, 0x3f7fffffU, 0U, "just outside lower bound" },
	{ 0x00800000U, 0x3f800001U, 0U, "just inside lower bound" },
	{ 0x5f800000U, 0x5f000000U, 0U, "exact upper bound 2^127" },
	{ 0x5f800000U, 0x5f000001U, 0U, "just outside upper bound" },
	{ 0x7f7fffffU, 0x40000000U, 0U, "overflow" },
	{ 0x3f800001U, 0x3fc00000U, 0U, "halfway product" },
	{ 0x00000000U, 0x7f800000U, 0U, "zero times infinity" }
};
static const Input div_inputs[] = {
	{ 0x40c00000U, 0x40400000U, 0U, "exact quotient" },
	{ 0x3f800000U, 0x40400000U, 0U, "inexact quotient" },
	{ 0x00800000U, 0x3f800000U, 0U, "exact lower bound 2^-126" },
	{ 0x00800000U, 0x3f800001U, 0U, "rounding at lower bound from below" },
	{ 0x00800000U, 0x3f7fffffU, 0U, "just inside lower bound" },
	{ 0x7f000000U, 0x3f800000U, 0U, "exact upper bound 2^127" },
	{ 0x7f000000U, 0x3f7fffffU, 0U, "just outside upper bound" },
	{ 0x7f7fffffU, 0x00800000U, 0U, "quotient overflow" },
	{ 0x3f800000U, 0x00000000U, 0U, "division by +zero" },
	{ 0x00000000U, 0x00000000U, 0U, "zero divided by zero" }
};
static const Input fused_inputs[] = {
	{ 0x3f800000U, 0x40000000U, 0x40400000U, "exact fused result" },
	{ 0x3f800001U, 0x3f800001U, 0xbf800000U, "fused differs from multiply then add" },
	{ 0x3fc00000U, 0x40000000U, 0xc0400000U, "product and addend cancel" },
	{ 0x5f800000U, 0x5f000000U, 0x00000000U, "exact upper bound 2^127" },
	{ 0x5f800000U, 0x5f000001U, 0x00000000U, "just outside upper bound" },
	{ 0x00800000U, 0x3f800000U, 0x80000001U, "just outside lower bound" },
	{ 0x00800000U, 0x3f800000U, 0x00000001U, "just inside lower bound" },
	{ 0x7f7fffffU, 0x40000000U, 0xff7fffffU, "large fused cancellation" },
	{ 0x7f800000U, 0x00000000U, 0x3f800000U, "infinity times zero plus one" }
};

static uint64 rng_state = RANDOM_SEED;
static unsigned failures;
static uint64 random64(void)
{
	uint64 x = rng_state;
	x ^= x >> 12U; x ^= x << 25U; x ^= x >> 27U; rng_state = x;
	return x * 0x2545f4914f6cdd1dULL;
}
static uint32 edge_float(void)
{
	static const uint8 exponents[] = { 0U, 1U, 2U, 125U, 126U, 127U, 128U, 129U, 252U, 253U, 254U, 255U };
	uint64 value = random64();
	uint32 exponent = exponents[(value >> 32U) % (sizeof(exponents) / sizeof(exponents[0]))];
	return (uint32)(value & 0x807fffffU) | (exponent << 23U);
}

static Result run(const Operation *operation, const Input *input, uint8 rm, std_bool disabled)
{
	TargetCoreType core;
	RiscvDecodedInsn decoded;
	Result result;
	uint64 before;
	memset(&core, 0, sizeof(core)); memset(&decoded, 0, sizeof(decoded));
	core.reg.pc = 0x100U; core.reg.csr.mstatus = INITIAL_MSTATUS; core.reg.fcsr = INITIAL_FCSR;
	core.reg.f[1] = 0xffffffff00000000ULL | input->left;
	core.reg.f[2] = 0xffffffff00000000ULL | input->right;
	core.reg.f[3] = 0xffffffff00000000ULL | input->addend;
	decoded.pc = core.reg.pc; decoded.instruction = 0x00000053U;
	decoded.rd = 4U; decoded.rs1 = 1U; decoded.rs2 = 2U; decoded.rs3 = 3U;
	decoded.length = 4U; decoded.aux = rm;
	riscv_fpu_fast_test_set_disabled(disabled);
	before = riscv_fpu_fast_test_hits();
	result.retired = operation->execute(&core, &decoded);
	result.fast_hits = riscv_fpu_fast_test_hits() - before;
	result.destination = core.reg.f[4]; result.pc = core.reg.pc;
	result.mstatus = core.reg.csr.mstatus; result.fcsr = core.reg.fcsr;
	return result;
}
static std_bool same_result(const Result *fast, const Result *flags)
{
	return fast->destination == flags->destination && fast->fcsr == flags->fcsr &&
		fast->mstatus == flags->mstatus && fast->retired == flags->retired && fast->pc == flags->pc;
}
static std_bool compare(const Operation *operation, const Input *input, uint8 rm, const char *kind,
	uint64 index, uint64 *fast_count, uint64 *allowed_count)
{
	Result fast = run(operation, input, rm, FALSE);
	Result flags = run(operation, input, rm, TRUE);
	*fast_count += fast.fast_hits; (*allowed_count)++;
	if ((flags.fast_hits != 0U) || !same_result(&fast, &flags)) {
		if (failures++ < 20U) {
			printf("FAIL %s %s[%" PRIu64 "] rm=%u seed=0x%016" PRIx64
				" inputs=%08" PRIx32 ",%08" PRIx32 ",%08" PRIx32 " name=%s\n",
				operation->name, kind, index, rm, (uint64)RANDOM_SEED,
				input->left, input->right, input->addend, input->name ? input->name : "random");
			printf(" fast f4=%016" PRIx64 " fcsr=%08" PRIx32 " ms=%016" PRIx64
				" ret=%d pc=%016" PRIx64 " hits=%" PRIu64 "\n",
				fast.destination, fast.fcsr, fast.mstatus, fast.retired, fast.pc, fast.fast_hits);
			printf(" flag f4=%016" PRIx64 " fcsr=%08" PRIx32 " ms=%016" PRIx64
				" ret=%d pc=%016" PRIx64 " hits=%" PRIu64 "\n",
				flags.destination, flags.fcsr, flags.mstatus, flags.retired, flags.pc, flags.fast_hits);
		}
		return FALSE;
	}
	return TRUE;
}
static std_bool run_list(const Operation *operation, const Input *inputs, size_t count,
	const char *kind, uint64 *fast_count, uint64 *allowed_count)
{
	size_t i; uint8 rm;
	for (i = 0U; i < count; i++) {
		for (rm = 0U; rm <= 7U; rm += 7U) {
			if (!compare(operation, &inputs[i], rm, kind, i, fast_count, allowed_count)) return FALSE;
		}
	}
	return TRUE;
}
static const Input *specific_inputs(size_t operation_index, size_t *count)
{
	if (operation_index < 2U) { *count = sizeof(add_inputs) / sizeof(add_inputs[0]); return add_inputs; }
	if (operation_index == 2U) { *count = sizeof(mul_inputs) / sizeof(mul_inputs[0]); return mul_inputs; }
	if (operation_index == 3U) { *count = sizeof(div_inputs) / sizeof(div_inputs[0]); return div_inputs; }
	*count = sizeof(fused_inputs) / sizeof(fused_inputs[0]); return fused_inputs;
}

static std_bool hand_checks(void)
{
	struct { size_t operation; Input input; uint32 bits, new_flags; } checks[] = {
		{ 0U, { 0x3f800000U, 0x3f800000U, 0U, "1 + 1 = 2" }, 0x40000000U, 0U },
		{ 0U, { 0x3f800000U, 0x33800000U, 0U, "1 + 2^-24 ties to 1" }, 0x3f800000U, FFLAG_NX },
		{ 2U, { 0x3fc00000U, 0x40000000U, 0U, "1.5 * 2 = 3" }, 0x40400000U, 0U },
#if !CPU_CONFIG_ERBIUM_COMPAT
		{ 3U, { 0x40c00000U, 0x40400000U, 0U, "6 / 3 = 2" }, 0x40000000U, 0U },
#endif
		{ 4U, { 0x3f800000U, 0x40000000U, 0x40400000U, "1 * 2 + 3 = 5" }, 0x40a00000U, 0U }
	};
	size_t i;
	for (i = 0U; i < sizeof(checks) / sizeof(checks[0]); i++) {
		Result r = run(&operations[checks[i].operation], &checks[i].input, 0U, TRUE);
		uint64 expected = 0xffffffff00000000ULL | checks[i].bits;
		if (r.destination != expected || r.fcsr != (INITIAL_FCSR | checks[i].new_flags) ||
			r.mstatus != (INITIAL_MSTATUS | (3ULL << 13U)) ||
			r.retired != RISCV_EXEC_RETIRED || r.pc != 0x104U) {
			printf("FAIL hand check %s: f4=%016" PRIx64 " fcsr=%08" PRIx32 "\n",
				checks[i].input.name, r.destination, r.fcsr);
			failures++; return FALSE;
		}
	}
	return TRUE;
}
static std_bool non_rne_check(const Operation *operation)
{
	Input input = { 0x3f800001U, 0x33800000U, 0xbf800000U, "non-RNE gate" };
	Result allowed = run(operation, &input, 1U, FALSE);
	Result disabled = run(operation, &input, 1U, TRUE);
	if (allowed.fast_hits || disabled.fast_hits || !same_result(&allowed, &disabled)) {
		printf("FAIL %s non-RNE gate: hits=%" PRIu64 ",%" PRIu64 "\n",
			operation->name, allowed.fast_hits, disabled.fast_hits);
		failures++; return FALSE;
	}
	return TRUE;
}
static std_bool lower_bound_gate_check(const Operation *operation, size_t operation_index)
{
	Input input = { 0x00800000U, 0x00000000U, 0x00000000U, "exact lower-bound gate" };
	Result allowed, disabled;

	if (operation_index >= 2U) input.right = 0x3f800000U;
	allowed = run(operation, &input, 0U, FALSE);
	disabled = run(operation, &input, 0U, TRUE);
	if (allowed.fast_hits != 0U || !same_result(&allowed, &disabled)) {
		printf("FAIL %s exact lower-bound gate: hits=%" PRIu64 "\n",
			operation->name, allowed.fast_hits);
		failures++; return FALSE;
	}
	return TRUE;
}
static std_bool test_operation(size_t operation_index)
{
	const Operation *operation = &operations[operation_index];
	const Input *specific; Input input;
	size_t specific_count;
	uint64 fast_count = 0U, allowed_count = 0U;
	uint32 i; uint8 rm;
#if CPU_CONFIG_ERBIUM_COMPAT
	if (operation_index == 3U) {
		printf("SKIP fdiv.s fast path (Erbium defines fdiv.s as an illegal-operation trap)\n");
		return TRUE;
	}
#endif
	if (!run_list(operation, common_inputs, sizeof(common_inputs) / sizeof(common_inputs[0]),
		"common", &fast_count, &allowed_count)) return FALSE;
	specific = specific_inputs(operation_index, &specific_count);
	if (!run_list(operation, specific, specific_count, "boundary", &fast_count, &allowed_count)) return FALSE;
	if (!lower_bound_gate_check(operation, operation_index)) return FALSE;
	if (!non_rne_check(operation)) return FALSE;
	for (i = 0U; i < RANDOM_INPUTS; i++) {
		input.left = (uint32)random64(); input.right = (uint32)random64();
		input.addend = (uint32)random64(); input.name = NULL;
		for (rm = 0U; rm <= 7U; rm += 7U)
			if (!compare(operation, &input, rm, "random", i, &fast_count, &allowed_count)) return FALSE;
	}
	for (i = 0U; i < EDGE_INPUTS; i++) {
		input.left = edge_float(); input.right = edge_float(); input.addend = edge_float(); input.name = NULL;
		for (rm = 0U; rm <= 7U; rm += 7U)
			if (!compare(operation, &input, rm, "edge-random", i, &fast_count, &allowed_count)) return FALSE;
	}
	printf("PASS %-8s fast=%" PRIu64 " flag-fallback=%" PRIu64 " forced-flag=%" PRIu64 "\n",
		operation->name, fast_count, allowed_count - fast_count, allowed_count);
	if (!fast_count || fast_count == allowed_count) {
		printf("FAIL %s did not exercise both automatic paths\n", operation->name);
		failures++; return FALSE;
	}
	return TRUE;
}
static std_bool erbium_input_denormal_check(void)
{
#if CPU_CONFIG_ERBIUM_COMPAT
	Input input = { 0x00000001U, 0x3f800000U, 0U, "Erbium input denormal" };
	Result fast = run(&operations[0], &input, 0U, FALSE);
	Result flags = run(&operations[0], &input, 0U, TRUE);
	if (!same_result(&fast, &flags) || !(fast.fcsr & FFLAG_INPUT_DENORM) ||
		fast.destination != 0xffffffff3f800000ULL) {
		printf("FAIL Erbium input-denormal flag: f4=%016" PRIx64 " fcsr=%08" PRIx32 "\n",
			fast.destination, fast.fcsr);
		failures++; return FALSE;
	}
	printf("PASS Erbium FFLAG_INPUT_DENORM fast/flag equivalence\n");
#endif
	return TRUE;
}
int main(void)
{
	size_t i;
	printf("athrill-fpu-fast seed=0x%016" PRIx64 " random=%u edge-random=%u mode=%s\n",
		(uint64)RANDOM_SEED, RANDOM_INPUTS, EDGE_INPUTS,
		CPU_CONFIG_ERBIUM_COMPAT ? "Erbium" : "standard");
	if (!hand_checks() || !erbium_input_denormal_check()) return 1;
	for (i = 0U; i < sizeof(operations) / sizeof(operations[0]); i++)
		if (!test_operation(i)) return 1;
	printf("%s\n", failures == 0U ? "SELFTEST PASS" : "SELFTEST FAIL");
	return failures == 0U ? 0 : 1;
}
