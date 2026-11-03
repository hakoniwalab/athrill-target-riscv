/*
 * Erbium UART transmitter timing on the virtual clock.
 *
 * Expected behaviour (Shakti UART as used by Erbium):
 *   - a 16-character TX FIFO sends one character every 30 clocks, at
 *     multiples of 30 on the virtual clock;
 *   - status bit 0 (TX empty) and bit 1 (TX full) follow the FIFO;
 *   - a character written to a full FIFO is lost;
 *   - with the TX-empty interrupt enabled, the PLIC source is high exactly
 *     while the FIFO is empty, and a clock skip may not jump past the
 *     moment the FIFO drains.
 * The UART is driven here the way Athrill's main loop drives it: one
 * device tick at clock c before the CPU executes at clock c.
 */
#include "peripheral/erbium_devices.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define UART_BASE 0x02004000ULL
#define UART_TX (UART_BASE + 0x08U)
#define UART_STATUS (UART_BASE + 0x18U)
#define UART_INTERRUPT_ENABLE (UART_BASE + 0x30U)
#define STATUS_TX_EMPTY 0x1U
#define STATUS_TX_FULL 0x2U
#define IE_TX_EMPTY 0x1U

static unsigned failures;
static unsigned host_characters;
static std_bool plic_level;

std_bool dbg_serial_putchar(uint8 channel, uint8 data)
{
	(void)channel;
	(void)data;
	host_characters++;
	return TRUE;
}

void intc_plic_set_source(uint32 source, std_bool level)
{
	if (source == 3U) {
		plic_level = level;
	}
}

static DeviceClockType device_clock;

static void run_to(uint64 clock)
{
	device_clock.clock = clock;
	device_clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	erbium_uart_tick(&device_clock);
}

static uint32 status(void)
{
	uint32 value;

	erbium_uart_ops.get_data32(NULL, 0, UART_STATUS, &value);
	return value;
}

static void write_tx(uint8 c)
{
	erbium_uart_ops.put_data8(NULL, 0, UART_TX, c);
}

static void check(const char *name, int passed)
{
	printf("%s %s\n", passed ? "PASS" : "FAIL", name);
	if (!passed) {
		failures++;
	}
}

static void start(void)
{
	erbium_uart_reset();
	host_characters = 0U;
	run_to(0U);
}

static void check_one_character(void)
{
	start();
	check("idle: TX empty, not full", status() == STATUS_TX_EMPTY);
	run_to(5U);
	write_tx('a');
	check("written at 5: not empty", (status() & STATUS_TX_EMPTY) == 0U);
	run_to(29U);
	check("clock 29: still not empty", (status() & STATUS_TX_EMPTY) == 0U);
	run_to(30U);
	check("clock 30: empty again", status() == STATUS_TX_EMPTY);
	check("character reached the host", host_characters == 1U);
}

static void check_write_on_boundary(void)
{
	start();
	run_to(30U);
	write_tx('a');
	run_to(59U);
	check("written at 30: busy until 59", (status() & STATUS_TX_EMPTY) == 0U);
	run_to(60U);
	check("written at 30: empty at 60", status() == STATUS_TX_EMPTY);
}

static void check_full_fifo(void)
{
	unsigned i;

	start();
	run_to(1U);
	for (i = 0U; i < 15U; i++) {
		write_tx('a');
	}
	check("15 characters: not full", (status() & STATUS_TX_FULL) == 0U);
	write_tx('a');
	check("16 characters: full", (status() & STATUS_TX_FULL) != 0U);
	write_tx('x');
	check("write to a full FIFO is lost", host_characters == 16U);
	run_to(29U);
	check("clock 29: still full", (status() & STATUS_TX_FULL) != 0U);
	run_to(30U);
	check("clock 30: one sent, not full", status() == 0U);
	run_to(479U);
	check("clock 479: last character still queued", (status() & STATUS_TX_EMPTY) == 0U);
	run_to(480U);
	check("clock 480: all 16 sent", status() == STATUS_TX_EMPTY);
}

static void check_clock_skip(void)
{
	start();
	run_to(10U);
	write_tx('a');
	write_tx('b');
	run_to(1000U);
	check("after a skip past the drain: empty", status() == STATUS_TX_EMPTY);
}

static void check_tx_empty_interrupt(void)
{
	start();
	erbium_uart_ops.put_data32(NULL, 0, UART_INTERRUPT_ENABLE, IE_TX_EMPTY);
	check("TX-empty interrupt on an empty FIFO is high", plic_level == TRUE);
	run_to(40U);
	write_tx('a');
	write_tx('b');
	check("interrupt drops when characters are queued", plic_level == FALSE);
	run_to(41U);
	check("skip limited to the drain at 90", device_clock.min_intr_interval == 49U);
	run_to(89U);
	check("clock 89: interrupt still low", plic_level == FALSE);
	run_to(90U);
	check("clock 90: interrupt high", plic_level == TRUE);
	check("no skip limit once empty", device_clock.min_intr_interval == DEVICE_CLOCK_MAX_INTERVAL);
	erbium_uart_ops.put_data32(NULL, 0, UART_INTERRUPT_ENABLE, 0U);
	check("disabling the interrupt drops the line at once", plic_level == FALSE);
}

static void check_halfword_receive(void)
{
	uint16 value;

	start();
	(void)erbium_uart_receive('A');
	(void)erbium_uart_receive('B');
	erbium_uart_ops.get_data16(NULL, 0, UART_BASE + 0x10U, &value);
	check("halfword read of RX takes one character", value == 'A');
	erbium_uart_ops.get_data16(NULL, 0, UART_BASE + 0x10U, &value);
	check("next halfword read takes the next one", value == 'B');
}

static uint8 read_rx(void)
{
	uint32 value;

	erbium_uart_ops.get_data32(NULL, 0, UART_BASE + 0x10U, &value);
	return (uint8)value;
}

static uint32 rx_ready(void)
{
	return status() & 0x4U;
}

static void check_rx_file(void)
{
	char path[] = "/tmp/athrill-uart-rx-XXXXXX";
	int fd = mkstemp(path);
	FILE *file = fdopen(fd, "wb");
	unsigned i;

	fputs("AB", file);
	fflush(file);
	start();
	erbium_uart_open_rx_file(path);
	run_to(1U);
	check("RX file: first character not before the edge at 30", rx_ready() == 0U);
	check("RX file: skip limited to the edge at 30", device_clock.min_intr_interval == 29U);
	run_to(30U);
	check("RX file: 'A' at 30", rx_ready() != 0U && read_rx() == 'A');
	run_to(59U);
	check("RX file: nothing more at 59", rx_ready() == 0U);
	run_to(60U);
	check("RX file: 'B' at 60", read_rx() == 'B');
	run_to(90U);
	run_to(91U);
	check("RX file: end of file, skip limited to the next poll",
		rx_ready() == 0U && device_clock.min_intr_interval == 1000020U - 1U);
	fputs("C", file);
	fflush(file);
	run_to(1000109U);
	check("RX file: appended data not before the poll", rx_ready() == 0U);
	run_to(1000110U);
	check("RX file: appended 'C' at the poll", read_rx() == 'C');

	/* A full RX FIFO stops the input until the guest reads. */
	for (i = 0U; i < 20U; i++) {
		fputc('a' + (int)i, file);
	}
	fflush(file);
	for (i = 1U; i <= 25U; i++) {
		run_to(1000110U + i * 30U);
	}
	check("RX file: FIFO filled with 16 characters, the rest waits",
		read_rx() == 'a' && device_clock.min_intr_interval == DEVICE_CLOCK_MAX_INTERVAL);
	run_to(1000110U + 26U * 30U);
	check("RX file: input resumes after a read", rx_ready() != 0U);
	erbium_uart_open_rx_file(NULL);
	fclose(file);
	unlink(path);
}

int main(void)
{
	check_one_character();
	check_write_on_boundary();
	check_full_fifo();
	check_clock_skip();
	check_tx_empty_interrupt();
	check_halfword_receive();
	check_rx_file();
	printf("%s\n", failures == 0U ? "SELFTEST PASS" : "SELFTEST FAIL");
	return failures == 0U ? 0 : 1;
}
