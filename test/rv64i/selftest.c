/*
 * RV64I bare-metal self-test (Erbium mode).
 *
 * Expected values are written out by hand from the RISC-V specification;
 * none is computed by the emulator under test. Output goes to the
 * bring-up serial register; the last line is "SELFTEST PASS" or
 * "SELFTEST FAIL <count>".
 */
#include <stdint.h>

#define SERIAL (*(volatile uint8_t *)0x10000000UL)

static void test_putc(char c)
{
	SERIAL = (uint8_t)c;
}

#include "../common/test_report.h"

static volatile uint64_t memory_words[4];
static volatile uint32_t generated_code[2] __attribute__((aligned(4)));

static void check_arithmetic(void)
{
	volatile uint64_t a = 0x123456789abcdef0ULL;
	volatile uint64_t b = 0x1111111111111111ULL;
	int passed = 1;

	passed &= (a + b) == 0x23456789abcdf001ULL;
	passed &= (a - b) == 0x0123456789abcddfULL;
	passed &= (a ^ b) == 0x032547698badcfe1ULL;
	passed &= (a | b) == 0x133557799bbddff1ULL;
	passed &= (a & b) == 0x1010101010101010ULL;
	test_check("arithmetic", passed);
}

static void check_shifts(void)
{
	volatile uint64_t amount = 4U;
	volatile uint64_t value = 0x8000000000000001ULL;
	int passed = 1;

	passed &= (value << amount) == 0x10ULL;
	passed &= (value >> amount) == 0x0800000000000000ULL;
	passed &= ((int64_t)value >> amount) == (int64_t)0xf800000000000000ULL;
	test_check("shifts", passed);
}

static void check_compares(void)
{
	volatile int64_t minus_two = -2;
	volatile int64_t one = 1;
	volatile uint64_t big = (uint64_t)-2;
	int passed = 1;

	passed &= minus_two < one;
	passed &= !(big < 1U);
	passed &= ((uint64_t)2U < (uint64_t)3U);
	test_check("compares", passed);
}

static void check_branches(void)
{
	volatile uint64_t index;
	int sum = 0;

	for (index = 0U; index < 7U; index++) {
		if (index == 3U) {
			continue;
		}
		sum += (int)index;
	}
	test_check("branches", sum == 18);
}

static void check_loads(void)
{
	volatile int8_t *s8 = (volatile int8_t *)memory_words;
	volatile uint8_t *u8 = (volatile uint8_t *)memory_words;
	volatile int16_t *s16 = (volatile int16_t *)memory_words;
	volatile uint16_t *u16 = (volatile uint16_t *)memory_words;
	volatile int32_t *s32 = (volatile int32_t *)memory_words;
	volatile uint32_t *u32 = (volatile uint32_t *)memory_words;
	int passed = 1;

	memory_words[0] = 0x80ff8001ffffffffULL;
	passed &= s8[0] == -1;
	passed &= u8[0] == 255U;
	passed &= s16[1] == -1;
	passed &= u16[1] == 65535U;
	passed &= s32[0] == -1;
	passed &= u32[0] == 0xffffffffU;
	passed &= memory_words[0] == 0x80ff8001ffffffffULL;
	test_check("loads", passed);
}

static void check_stores(void)
{
	volatile uint8_t *bytes = (volatile uint8_t *)&memory_words[1];
	int passed = 1;

	bytes[0] = 0x12U;
	*(volatile uint16_t *)(bytes + 2) = 0x3456U;
	*(volatile uint32_t *)(bytes + 4) = 0x89abcdefU;
	memory_words[2] = 0x0123456789abcdefULL;
	passed &= bytes[0] == 0x12U;
	passed &= *(volatile uint16_t *)(bytes + 2) == 0x3456U;
	passed &= *(volatile uint32_t *)(bytes + 4) == 0x89abcdefU;
	passed &= memory_words[2] == 0x0123456789abcdefULL;
	test_check("stores", passed);
}

static void check_word_operations(void)
{
	int32_t max_int = (int32_t)0x7fffffffU;
	int32_t one = 1;
	int32_t min_int = (int32_t)0x80000000U;
	int32_t result;
	int passed = 1;

	asm volatile("addw %0, %1, %2" : "=r"(result) : "r"(max_int), "r"(one));
	passed &= result == (int32_t)0x80000000U;
	asm volatile("subw %0, %1, %2" : "=r"(result) : "r"(one), "r"(max_int));
	passed &= result == (int32_t)0x80000002U;
	asm volatile("slliw %0, %1, 1" : "=r"(result) : "r"(max_int));
	passed &= result == -2;
	asm volatile("sraiw %0, %1, 1" : "=r"(result) : "r"(min_int));
	passed &= result == (int32_t)0xc0000000U;
	test_check("word", passed);
}

static void check_misaligned_access(void)
{
	uint8_t *address = (uint8_t *)&memory_words[3];
	uint64_t written = 0x8877665544332211ULL;
	uint64_t read_back;

	/* Erbium performs misaligned scalar accesses instead of trapping. */
	asm volatile("sd %2, 1(%1)\n\tld %0, 1(%1)"
		: "=r"(read_back) : "r"(address), "r"(written) : "memory");
	test_check("misaligned", read_back == written);
}

static uint64_t call_generated_code(void)
{
	register uint64_t return_value asm("a0");

	asm volatile("jalr ra, 0(%1)"
		: "=r"(return_value) : "r"(generated_code) : "ra", "memory");
	return return_value;
}

static void check_code_modification(void)
{
	int passed = 1;

	/* li a0, 1 ; ret */
	generated_code[0] = 0x00100513U;
	generated_code[1] = 0x00008067U;
	passed &= call_generated_code() == 1U;
	/* Rewrite to li a0, 2: the pre-decoded cache must see the store. */
	generated_code[0] = 0x00200513U;
	passed &= call_generated_code() == 2U;
	test_check("code modification", passed);
}

static void check_erbium_counters(void)
{
	uint64_t cycles;
	uint64_t retired;

	asm volatile("csrr %0, mcycle" : "=r"(cycles));
	asm volatile("csrr %0, minstret" : "=r"(retired));
	/* Erbium reads the cycle and instret counters as zero. */
	test_check("erbium counters", cycles == 0U && retired == 0U);
}

static void check_fence_i_cause_30(void)
{
	uint64_t saved_mtvec;
	uint64_t handler;
	uint64_t cause = 0U;

	/* Erbium raises cause 30 for FENCE.I; the handler skips it. */
	asm volatile(
		"csrr %0, mtvec\n\t"
		"la %1, 1f\n\t"
		"csrw mtvec, %1\n\t"
		"fence.i\n\t"
		"j 2f\n"
		"1:\n\t"
		"csrr %2, mcause\n\t"
		"csrr t0, mepc\n\t"
		"addi t0, t0, 4\n\t"
		"csrw mepc, t0\n\t"
		"mret\n"
		"2:\n\t"
		"csrw mtvec, %0"
		: "=&r"(saved_mtvec), "=&r"(handler), "=&r"(cause)
		:
		: "t0", "memory");
	test_check("fence.i cause 30", cause == 30U);
}

int main(void)
{
	test_puts("Hello RV64I\n");
	check_arithmetic();
	check_shifts();
	check_compares();
	check_branches();
	check_loads();
	check_stores();
	check_word_operations();
	check_misaligned_access();
	check_code_modification();
	check_erbium_counters();
	check_fence_i_cause_30();
	test_report_finish();
	return 0;
}
