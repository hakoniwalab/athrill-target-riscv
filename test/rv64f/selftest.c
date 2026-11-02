#include <stdint.h>

#define SERIAL (*(volatile uint8_t *)0x10000000UL)

static void puts(const char *text)
{
	while (*text != '\0') {
		SERIAL = (uint8_t)*text++;
	}
}

static void result(const char *name, int passed)
{
	puts(passed ? "PASS " : "FAIL ");
	puts(name);
	puts("\n");
}

static uint32_t add_bits(uint32_t left, uint32_t right, unsigned rm)
{
	uint32_t result_bits;

	asm volatile("fmv.w.x f0,%0;fmv.w.x f1,%1" : : "r"(left), "r"(right));
	if (rm == 0U) {
		asm volatile("fadd.s f0,f0,f1,rne;fmv.x.w %0,f0" : "=r"(result_bits));
	}
	else if (rm == 1U) {
		asm volatile("fadd.s f0,f0,f1,rtz;fmv.x.w %0,f0" : "=r"(result_bits));
	}
	else if (rm == 3U) {
		asm volatile("fadd.s f0,f0,f1,rup;fmv.x.w %0,f0" : "=r"(result_bits));
	}
	else {
		asm volatile("fadd.s f0,f0,f1,rmm;fmv.x.w %0,f0" : "=r"(result_bits));
	}
	return result_bits;
}

static uint64_t cause30_fdiv(void)
{
	uint64_t old_mtvec;
	uint64_t cause = 0U;

	asm volatile(
		"csrr %0,mtvec;la t1,1f;csrw mtvec,t1;"
		"li t0,0x3f800000;fmv.w.x f0,t0;fdiv.s f1,f0,f0;"
		"j 2f;1:csrr %1,mcause;csrr t0,mepc;addi t0,t0,4;"
		"csrw mepc,t0;mret;2:csrw mtvec,%0"
		: "=&r"(old_mtvec), "+r"(cause) : : "t0", "t1", "f0", "f1", "memory");
	return cause;
}

int main(void)
{
	uint32_t flags;
	uint32_t value;
	uint32_t class_value;
	uint64_t cause;

	asm volatile("li t0,0x6000;csrs mstatus,t0;csrw fcsr,zero" : : : "t0");
	result("round rne", add_bits(0x3f800000U, 0x33800000U, 0U) == 0x3f800000U);
	result("round rtz", add_bits(0x3f800000U, 0x33800000U, 1U) == 0x3f800000U);
	result("round rup", add_bits(0x3f800000U, 0x33800000U, 3U) == 0x3f800001U);
	result("round rmm", add_bits(0x3f800000U, 0x33800000U, 4U) == 0x3f800001U);
	asm volatile("csrw fflags,zero;li t0,0x7f800000;fmv.w.x f0,t0;"
		"fmv.w.x f1,zero;fmul.s f2,f0,f1;csrr %0,fflags"
		: "=r"(flags) : : "t0", "f0", "f1", "f2");
	result("invalid flag", (flags & 0x10U) != 0U);
	asm volatile("li t0,0x7fc00000;fmv.w.x f0,t0;fclass.s %0,f0"
		: "=r"(class_value) : : "t0", "f0");
	result("nan boxing", class_value == (1U << 9U));
	cause = cause30_fdiv();
	if (cause == 30U) {
		asm volatile("csrw fflags,zero;li t0,1;fmv.w.x f0,t0;"
			"fmv.w.x f1,zero;fadd.s f2,f0,f1;fmv.x.w %0,f2;csrr %1,fflags"
			: "=r"(value), "=r"(flags) : : "t0", "f0", "f1", "f2");
		result("erbium cause 30", 1);
		result("erbium denormal", value == 0U && (flags & 0x80000000U) != 0U);
	}
	else {
		result("standard fdiv", cause == 0U);
		asm volatile("csrw fflags,zero;li t0,1;fmv.w.x f0,t0;"
			"fmv.w.x f1,zero;fadd.s f2,f0,f1;fmv.x.w %0,f2"
			: "=r"(value) : : "t0", "f0", "f1", "f2");
		result("standard denormal", value == 1U);
	}
	puts("FP SELFTEST DONE\n");
	return 0;
}
