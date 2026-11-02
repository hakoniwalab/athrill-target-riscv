/*
 * Erbium device integration test.
 *
 * Prints through the Shakti UART, takes three machine-timer interrupts
 * (sleeping in WFI in between) and echoes one character received on the
 * UART through the PLIC. Then it queues characters and sleeps in WFI until
 * the TX-empty interrupt reports that the transmitter has drained (the
 * device config enables clock skip, so this checks that a skip stops at
 * the drain). The last line is "SELFTEST PASS" or "SELFTEST FAIL <count>".
 */
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define REG64(address) (*(volatile uint64_t *)(uintptr_t)(address))

#define UART_TX 0x02004008U
#define UART_RX 0x02004010U
#define UART_STATUS 0x02004018U
#define UART_STATUS_TX_EMPTY 0x1U
#define UART_STATUS_TX_FULL 0x2U
#define UART_INTERRUPT_ENABLE 0x02004030U
#define UART_RX_INTERRUPT 0x4U
#define UART_TX_EMPTY_INTERRUPT 0x1U

#define MTIME 0x80f40200U
#define MTIMECMP 0x80f40208U
#define TIMER_PERIOD 100U

#define PLIC_ENABLE 0xa0002000U
#define PLIC_THRESHOLD 0xa0200000U
#define PLIC_CLAIM 0xa0200004U
#define PLIC_SOURCE_UART 3U

#define MCAUSE_INTERRUPT (1ULL << 63)
#define MCAUSE_MACHINE_TIMER 7U
#define MCAUSE_MACHINE_EXTERNAL 11U
#define MIE_MTIE (1ULL << 7)
#define MIE_MEIE (1ULL << 11)

#define EXPECTED_TIMER_INTERRUPTS 3U
#define EXPECTED_RX_CHARACTERS 1U

static void test_putc(char c)
{
	while ((REG32(UART_STATUS) & UART_STATUS_TX_FULL) != 0U) {
	}
	REG32(UART_TX) = (uint32_t)(uint8_t)c;
}

#include "../common/test_report.h"

static volatile unsigned timer_interrupts;
static volatile unsigned rx_characters;
static volatile unsigned unexpected_traps;
static volatile unsigned tx_empty_interrupts;
static volatile unsigned tx_empty_seen_in_status;

static void handle_timer_interrupt(void)
{
	timer_interrupts++;
	REG64(MTIMECMP) = REG64(MTIME) + TIMER_PERIOD;
	test_puts("timer ");
	test_put_decimal(timer_interrupts);
	test_puts("\n");
}

static void handle_external_interrupt(void)
{
	uint32_t source = REG32(PLIC_CLAIM);
	char received;

	if ((source == PLIC_SOURCE_UART)
		&& ((REG32(UART_INTERRUPT_ENABLE) & UART_TX_EMPTY_INTERRUPT) != 0U)) {
		/* The TX-empty interrupt is level: switch it off before returning. */
		if ((REG32(UART_STATUS) & UART_STATUS_TX_EMPTY) != 0U) {
			tx_empty_seen_in_status++;
		}
		REG32(UART_INTERRUPT_ENABLE) = 0U;
		tx_empty_interrupts++;
	}
	else if (source == PLIC_SOURCE_UART) {
		received = (char)REG32(UART_RX);
		test_puts("echo ");
		test_putc(received);
		test_puts("\n");
		rx_characters++;
	}
	REG32(PLIC_CLAIM) = source;
}

void trap_handler(void) __attribute__((interrupt("machine")));

void trap_handler(void)
{
	uint64_t cause;

	asm volatile("csrr %0, mcause" : "=r"(cause));
	if (cause == (MCAUSE_INTERRUPT | MCAUSE_MACHINE_TIMER)) {
		handle_timer_interrupt();
	}
	else if (cause == (MCAUSE_INTERRUPT | MCAUSE_MACHINE_EXTERNAL)) {
		handle_external_interrupt();
	}
	else {
		unexpected_traps++;
		test_puts("unexpected trap\n");
		REG64(MTIMECMP) = ~0ULL;
		asm volatile("csrci mstatus, 8");
	}
}

int main(void)
{
	test_puts("erbium device test\n");
	REG32(PLIC_THRESHOLD) = 0U;
	REG32(PLIC_ENABLE) = 1U << PLIC_SOURCE_UART;
	REG32(UART_INTERRUPT_ENABLE) = UART_RX_INTERRUPT;
	test_puts("plic armed\n");
	REG64(MTIMECMP) = REG64(MTIME) + TIMER_PERIOD;
	test_puts("timer armed\n");
	asm volatile("csrs mie, %0" : : "r"(MIE_MTIE | MIE_MEIE));
	asm volatile("csrsi mstatus, 8");
	test_puts("interrupts on\n");
	while (unexpected_traps == 0U
		&& (timer_interrupts < EXPECTED_TIMER_INTERRUPTS
			|| rx_characters < EXPECTED_RX_CHARACTERS)) {
		asm volatile("wfi");
	}
	REG64(MTIMECMP) = ~0ULL;
	REG32(UART_INTERRUPT_ENABLE) = 0U;
	/* Queue a line, then sleep until the transmitter has drained. */
	test_puts("tx drain\n");
	REG32(UART_INTERRUPT_ENABLE) = UART_TX_EMPTY_INTERRUPT;
	while (unexpected_traps == 0U && tx_empty_interrupts == 0U) {
		asm volatile("wfi");
	}
	asm volatile("csrci mstatus, 8");
	test_check("timer interrupts", timer_interrupts == EXPECTED_TIMER_INTERRUPTS);
	test_check("uart rx interrupt", rx_characters == EXPECTED_RX_CHARACTERS);
	test_check("tx empty interrupt", tx_empty_interrupts == 1U);
	test_check("tx empty in status at the interrupt", tx_empty_seen_in_status == 1U);
	test_check("no unexpected trap", unexpected_traps == 0U);
	test_report_finish();
	for (;;) {
		asm volatile("wfi");
	}
}
