/*
 * RV64F bare-metal self-test.
 *
 * Build with -DERBIUM_MODE=1 for an Athrill built with Erbium behaviour
 * (the default) and -DERBIUM_MODE=0 for the standard RV64 behaviour.
 * The mode is fixed at build time so that a wrong behaviour in either
 * mode fails instead of selecting the other mode's expectations.
 *
 * Expected values are worked out by hand from IEEE 754 and the RISC-V
 * specification. The last line is "SELFTEST PASS" or
 * "SELFTEST FAIL <count>".
 */
#include <stdint.h>

#ifndef ERBIUM_MODE
#error "build with -DERBIUM_MODE=1 or -DERBIUM_MODE=0"
#endif

#define SERIAL (*(volatile uint8_t *)0x10000000UL)

#define FFLAG_NX 0x01U
#define FFLAG_NV 0x10U
#define FCSR_ERBIUM_INPUT_DENORM 0x80000000U

#define FLOAT_ONE 0x3f800000U
#define FLOAT_ULP_OF_ONE_HALF 0x33800000U	/* 2^-24: half an ulp of 1.0 */
#define FLOAT_INFINITY 0x7f800000U
#define FLOAT_QUIET_NAN 0x7fc00000U
#define FLOAT_SMALLEST_DENORMAL 0x00000001U

static void test_putc(char c)
{
	SERIAL = (uint8_t)c;
}

#include "../common/test_report.h"

enum rounding_mode {
	ROUND_RNE,
	ROUND_RTZ,
	ROUND_RUP,
	ROUND_RMM
};

static uint32_t add_with_rounding(uint32_t left, uint32_t right, enum rounding_mode mode)
{
	uint32_t result_bits;

	asm volatile("fmv.w.x f0, %0\n\tfmv.w.x f1, %1" : : "r"(left), "r"(right) : "f0", "f1");
	switch (mode) {
	case ROUND_RNE:
		asm volatile("fadd.s f2, f0, f1, rne\n\tfmv.x.w %0, f2"
			: "=r"(result_bits) : : "f2");
		break;
	case ROUND_RTZ:
		asm volatile("fadd.s f2, f0, f1, rtz\n\tfmv.x.w %0, f2"
			: "=r"(result_bits) : : "f2");
		break;
	case ROUND_RUP:
		asm volatile("fadd.s f2, f0, f1, rup\n\tfmv.x.w %0, f2"
			: "=r"(result_bits) : : "f2");
		break;
	default:
		asm volatile("fadd.s f2, f0, f1, rmm\n\tfmv.x.w %0, f2"
			: "=r"(result_bits) : : "f2");
		break;
	}
	return result_bits;
}

static void check_add_rounding(void)
{
	/* 1.0 + 2^-24 lies exactly halfway between 1.0 and 1.0 + 2^-23. */
	test_check("round rne", add_with_rounding(FLOAT_ONE, FLOAT_ULP_OF_ONE_HALF, ROUND_RNE)
		== FLOAT_ONE);
	test_check("round rtz", add_with_rounding(FLOAT_ONE, FLOAT_ULP_OF_ONE_HALF, ROUND_RTZ)
		== FLOAT_ONE);
	test_check("round rup", add_with_rounding(FLOAT_ONE, FLOAT_ULP_OF_ONE_HALF, ROUND_RUP)
		== FLOAT_ONE + 1U);
	test_check("round rmm", add_with_rounding(FLOAT_ONE, FLOAT_ULP_OF_ONE_HALF, ROUND_RMM)
		== FLOAT_ONE + 1U);
}

static uint32_t fused_multiply_add_rmm(uint32_t left, uint32_t right, uint32_t addend)
{
	uint32_t result_bits;

	asm volatile(
		"fmv.w.x f0, %1\n\t"
		"fmv.w.x f1, %2\n\t"
		"fmv.w.x f2, %3\n\t"
		"fmadd.s f3, f0, f1, f2, rmm\n\t"
		"fmv.x.w %0, f3"
		: "=r"(result_bits) : "r"(left), "r"(right), "r"(addend) : "f0", "f1", "f2", "f3");
	return result_bits;
}

static void check_fused_rmm(void)
{
	/*
	 * (1 + 2^-23) * (1 - 2^-24) = 1 + 2^-24 - 2^-47, just below the
	 * midpoint 1 + 2^-24 between 1.0 and 1.0 + 2^-23. The addend moves the
	 * exact result onto the midpoint or by 2^-71 / 2^-70 next to it; those
	 * offsets are below double precision, so only an exact tie test can
	 * tell them apart.
	 */
	const uint32_t one_plus_ulp = 0x3f800001U;	/* 1 + 2^-23 */
	const uint32_t one_minus_half_ulp = 0x3f7fffffU;	/* 1 - 2^-24 */

	test_check("fmadd rmm exact tie rounds away",
		fused_multiply_add_rmm(one_plus_ulp, one_minus_half_ulp, 0x28000000U)	/* 2^-47 */
			== one_plus_ulp);
	test_check("fmadd rmm just below the tie rounds down",
		fused_multiply_add_rmm(one_plus_ulp, one_minus_half_ulp, 0x27ffffffU)	/* 2^-47 - 2^-71 */
			== FLOAT_ONE);
	test_check("fmadd rmm just above the tie rounds up",
		fused_multiply_add_rmm(one_plus_ulp, one_minus_half_ulp, 0x28000001U)	/* 2^-47 + 2^-70 */
			== one_plus_ulp);
}

static int32_t convert_to_int_rmm(uint32_t value_bits, uint32_t *flags)
{
	int32_t result;

	asm volatile(
		"csrw fflags, zero\n\t"
		"fmv.w.x f0, %2\n\t"
		"fcvt.w.s %0, f0, rmm\n\t"
		"csrr %1, fflags"
		: "=r"(result), "=r"(*flags) : "r"(value_bits) : "f0");
	return result;
}

static void check_convert_rmm(void)
{
	/* RMM rounds ties away from zero, also for negative values. */
	static const struct {
		const char *name;
		uint32_t value_bits;
		int32_t expected;
	} cases[] = {
		{ "fcvt rmm +0.5", 0x3f000000U, 1 },
		{ "fcvt rmm -0.5", 0xbf000000U, -1 },
		{ "fcvt rmm +1.5", 0x3fc00000U, 2 },
		{ "fcvt rmm -1.5", 0xbfc00000U, -2 },
		{ "fcvt rmm +2.5", 0x40200000U, 3 },
		{ "fcvt rmm -2.5", 0xc0200000U, -3 },
		{ "fcvt rmm -2.25", 0xc0100000U, -2 },
		{ "fcvt rmm -2.75", 0xc0300000U, -3 },
	};
	unsigned index;
	uint32_t flags;
	int32_t result;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		result = convert_to_int_rmm(cases[index].value_bits, &flags);
		test_check(cases[index].name,
			result == cases[index].expected && (flags & FFLAG_NX) != 0U);
	}
}

static void check_invalid_flag(void)
{
	uint32_t flags;

	/* infinity * 0 is invalid. */
	asm volatile(
		"csrw fflags, zero\n\t"
		"fmv.w.x f0, %1\n\t"
		"fmv.w.x f1, zero\n\t"
		"fmul.s f2, f0, f1\n\t"
		"csrr %0, fflags"
		: "=r"(flags) : "r"(FLOAT_INFINITY) : "f0", "f1", "f2");
	test_check("invalid flag", (flags & FFLAG_NV) != 0U);
}

static void check_quiet_nan_class(void)
{
	uint32_t class_bits;

	asm volatile("fmv.w.x f0, %1\n\tfclass.s %0, f0"
		: "=r"(class_bits) : "r"(FLOAT_QUIET_NAN) : "f0");
	test_check("quiet nan class", class_bits == (1U << 9));
}

static uint64_t fdiv_trap_cause(void)
{
	uint64_t saved_mtvec;
	uint64_t cause = 0U;

	asm volatile(
		"csrr %0, mtvec\n\t"
		"la t1, 1f\n\t"
		"csrw mtvec, t1\n\t"
		"li t0, 0x3f800000\n\t"
		"fmv.w.x f0, t0\n\t"
		"fdiv.s f1, f0, f0\n\t"
		"j 2f\n"
		"1:\n\t"
		"csrr %1, mcause\n\t"
		"csrr t0, mepc\n\t"
		"addi t0, t0, 4\n\t"
		"csrw mepc, t0\n\t"
		"mret\n"
		"2:\n\t"
		"csrw mtvec, %0"
		: "=&r"(saved_mtvec), "+r"(cause) : : "t0", "t1", "f0", "f1", "memory");
	return cause;
}

static void add_smallest_denormal(uint32_t *result_bits, uint32_t *fcsr)
{
	asm volatile(
		"csrw fcsr, zero\n\t"
		"fmv.w.x f0, %2\n\t"
		"fmv.w.x f1, zero\n\t"
		"fadd.s f2, f0, f1\n\t"
		"fmv.x.w %0, f2\n\t"
		"csrr %1, fcsr"
		: "=r"(*result_bits), "=r"(*fcsr) : "r"(FLOAT_SMALLEST_DENORMAL)
		: "f0", "f1", "f2");
}

static void check_mode_specific(void)
{
	uint64_t cause = fdiv_trap_cause();
	uint32_t result_bits;
	uint32_t fcsr;

	add_smallest_denormal(&result_bits, &fcsr);
#if ERBIUM_MODE
	/* Erbium has no FDIV.S in hardware and flushes denormal inputs. */
	test_check("erbium fdiv cause 30", cause == 30U);
	test_check("erbium denormal flushed", result_bits == 0U);
	test_check("erbium input denorm flag", (fcsr & FCSR_ERBIUM_INPUT_DENORM) != 0U);
#else
	test_check("standard fdiv does not trap", cause == 0U);
	test_check("standard denormal kept", result_bits == FLOAT_SMALLEST_DENORMAL);
#endif
}

int main(void)
{
	/* mstatus.FS = Initial so F instructions are allowed. */
	asm volatile("li t0, 0x2000\n\tcsrs mstatus, t0\n\tcsrw fcsr, zero" : : : "t0");
	check_add_rounding();
	check_convert_rmm();
	check_fused_rmm();
	check_invalid_flag();
	check_quiet_nan_class();
	check_mode_specific();
	test_report_finish();
	return 0;
}
