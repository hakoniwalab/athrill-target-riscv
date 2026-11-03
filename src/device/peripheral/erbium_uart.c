/*
 * Erbium UART register map (base 0x02004000):
 *   0x00 baud, 0x08 transmit, 0x10 receive, 0x18 status
 *   0x28 control, 0x30 interrupt enable, 0x40 receive threshold
 */
#include "erbium_devices.h"
#include "concrete_executor/target/dbg_target_serial.h"
#include "mmio_register.h"
#include <stdio.h>
#include <string.h>
#define UART_BASE 0x02004000ULL
#define UART_FIFO_SIZE 16U
#define UART_STATUS_TX_EMPTY (1U << 0)
#define UART_STATUS_TX_FULL (1U << 1)
#define UART_STATUS_RX_NOT_EMPTY (1U << 2)
#define UART_PLIC_SOURCE 3U

/*
 * Transmitter: a 16-character FIFO that sends one character every
 * UART_CHARACTER_CLOCKS clocks, at multiples of that period on the
 * virtual clock. The FIFO is described by the clock at which it becomes
 * empty, so its state follows the virtual clock also when Athrill skips
 * clocks while the CPU waits. A character written to a full FIFO is lost,
 * as on the hardware. Characters reach the host terminal when written.
 *
 * The interrupt line follows status and interrupt enable at once, like a
 * level output of the UART: after register accesses and received data,
 * and at the clock at which the TX FIFO drains.
 */
#define UART_CHARACTER_CLOCKS 30U

/*
 * Receive input from a file (DEVICE_CONFIG_ERBIUM_UART_RX_FILE), for runs
 * without the debugger: from the start of the file, one character per
 * UART_CHARACTER_CLOCKS at multiples of that period while the RX FIFO has
 * room, so the same file arrives at the same virtual times on every run.
 * Receiving a character takes one period, so the first one arrives at the
 * first period edge after the file is opened (clock 30 when it is opened
 * at reset).
 * At the end of the file, appended data is looked for every
 * UART_RX_FILE_POLL_CLOCKS (about one million clocks, a whole number of
 * UART periods so that the poll falls on a period edge).
 */
#define UART_RX_FILE_POLL_CLOCKS (UART_CHARACTER_CLOCKS * 33334U)

uint8 erbium_uart_data[0x100];
static FILE *rx_file;
static uint64 rx_file_next_clock;
static std_bool rx_file_at_end;
static uint32 baud, control, ie, rx_threshold;
static uint8 rx_fifo[UART_FIFO_SIZE];
static unsigned rx_head, rx_count;
static uint64 uart_now;
static uint64 tx_empty_clock;
static std_bool irq_level;

static uint32 tx_count(void)
{
	if (uart_now >= tx_empty_clock) {
		return 0U;
	}
	return (uint32)((tx_empty_clock - uart_now + UART_CHARACTER_CLOCKS - 1U) / UART_CHARACTER_CLOCKS);
}

static void tx_push(uint8 data)
{
	uint32 count = tx_count();

	if (count == UART_FIFO_SIZE) {
		return;
	}
	if (count == 0U) {
		tx_empty_clock = ((uart_now / UART_CHARACTER_CLOCKS) + 1U) * UART_CHARACTER_CLOCKS;
	}
	else {
		tx_empty_clock += UART_CHARACTER_CLOCKS;
	}
	if (erbium_uart_tcp_enabled()) {
		erbium_uart_tcp_put(data);
	}
	else {
		(void)dbg_serial_putchar(0, data);
	}
}

static uint32 status_value(void)
{
	uint32 count = tx_count();
	uint32 status = 0U;

	if (count == 0U) {
		status |= UART_STATUS_TX_EMPTY;
	}
	if (count == UART_FIFO_SIZE) {
		status |= UART_STATUS_TX_FULL;
	}
	if (rx_count != 0U) {
		status |= UART_STATUS_RX_NOT_EMPTY;
	}
	return status;
}

static std_bool irq_condition(void)
{
	if (((ie & UART_STATUS_TX_EMPTY) != 0U) && (tx_count() == 0U)) {
		return TRUE;
	}
	if (((ie & UART_STATUS_RX_NOT_EMPTY) != 0U) && (rx_count > rx_threshold)) {
		return TRUE;
	}
	return FALSE;
}

static void update_irq(void)
{
	std_bool level = irq_condition();

	if (level != irq_level) {
		irq_level = level;
		intc_plic_set_source(UART_PLIC_SOURCE, level);
	}
}

Std_ReturnType erbium_uart_receive(uint8 data)
{
	unsigned tail;
	if (rx_count == UART_FIFO_SIZE)return STD_E_LIMIT;
	tail = (rx_head + rx_count) % UART_FIFO_SIZE;
	rx_fifo[tail] = data;
	rx_count++;
	update_irq();
	return STD_E_OK;
}
static uint32 read_reg(AthrillAddrType a)
{
	uint32 v;
	switch ((unsigned)(a - UART_BASE)) {
	case 0:
		return baud;
	case 0x10:
		if (rx_count == 0U)return 0U;
		v = rx_fifo[rx_head];
		rx_head = (rx_head + 1U) % UART_FIFO_SIZE;
		rx_count--;
		update_irq();
		return v;
	case 0x18:
		return status_value();
	case 0x28:
		return control;
	case 0x30:
		return ie;
	case 0x40:
		return rx_threshold;
	default:
		return 0;
	}
}
static void write_reg(AthrillAddrType a, uint32 v)
{
	switch ((unsigned)(a - UART_BASE)) {
	case 0:
		baud = v;
		break;
	case 8:
		tx_push((uint8)v);
		update_irq();
		break;
	case 0x28:
		control = v;
		break;
	case 0x30:
		ie = v;
		update_irq();
		break;
	case 0x40:
		rx_threshold = v;
		update_irq();
		break;
	default:
		break;
	}
}
static Std_ReturnType get8(MpuAddressRegionType *region, CoreIdType core_id, AthrillAddrType address, uint8 *data)
{
	(void)region;
	(void)core_id;
	*data = (uint8)mmio_read(read_reg, address, 1U);
	return STD_E_OK;
}

static Std_ReturnType get16(MpuAddressRegionType *region, CoreIdType core_id, AthrillAddrType address, uint16 *data)
{
	(void)region;
	(void)core_id;
	*data = (uint16)mmio_read(read_reg, address, 2U);
	return STD_E_OK;
}

static Std_ReturnType get32(MpuAddressRegionType *region, CoreIdType core_id, AthrillAddrType address, uint32 *data)
{
	(void)region;
	(void)core_id;
	*data = mmio_read(read_reg, address, 4U);
	return STD_E_OK;
}

static Std_ReturnType put8(MpuAddressRegionType *region, CoreIdType core_id, AthrillAddrType address, uint8 data)
{
	(void)region;
	(void)core_id;
	mmio_write(write_reg, address, 1U, data);
	return STD_E_OK;
}

static Std_ReturnType put16(MpuAddressRegionType *region, CoreIdType core_id, AthrillAddrType address, uint16 data)
{
	(void)region;
	(void)core_id;
	mmio_write(write_reg, address, 2U, data);
	return STD_E_OK;
}

static Std_ReturnType put32(MpuAddressRegionType *region, CoreIdType core_id, AthrillAddrType address, uint32 data)
{
	(void)region;
	(void)core_id;
	mmio_write(write_reg, address, 4U, data);
	return STD_E_OK;
}

MpuAddressRegionOperationType erbium_uart_ops = {get8, get16, get32, put8, put16, put32, NULL};
void erbium_uart_reset(void)
{
	memset(erbium_uart_data, 0, sizeof(erbium_uart_data));
	memset(rx_fifo, 0, sizeof(rx_fifo));
	baud = 0;
	control = 0;
	ie = 0;
	rx_threshold = 0;
	rx_head = 0;
	rx_count = 0;
	uart_now = 0;
	tx_empty_clock = 0;
	irq_level = FALSE;
	intc_plic_set_source(UART_PLIC_SOURCE, FALSE);
}

void erbium_uart_open_rx_file(const char *path)
{
	if (rx_file != NULL) {
		fclose(rx_file);
		rx_file = NULL;
	}
	rx_file_next_clock = ((uart_now / UART_CHARACTER_CLOCKS) + 1U) * UART_CHARACTER_CLOCKS;
	rx_file_at_end = FALSE;
	if (path == NULL) {
		return;
	}
	rx_file = fopen(path, "rb");
	if (rx_file == NULL) {
		printf("WARNING: can not open UART input file %s\n", path);
	}
}

static void receive_from_file(DeviceClockType *clock)
{
	int c;
	uint64 next_edge;

	if (uart_now >= rx_file_next_clock && rx_count < UART_FIFO_SIZE
		&& (uart_now % UART_CHARACTER_CLOCKS) == 0U) {
		c = fgetc(rx_file);
		if (c == EOF) {
			clearerr(rx_file);
			rx_file_at_end = TRUE;
			rx_file_next_clock = uart_now + UART_RX_FILE_POLL_CLOCKS;
		}
		else {
			rx_file_at_end = FALSE;
			(void)erbium_uart_receive((uint8)c);
		}
	}
	/* A clock skip may not jump over the next character or poll. */
	if (rx_count < UART_FIFO_SIZE) {
		next_edge = rx_file_at_end ? rx_file_next_clock
			: ((uart_now / UART_CHARACTER_CLOCKS) + 1U) * UART_CHARACTER_CLOCKS;
		if (next_edge - uart_now < clock->min_intr_interval) {
			clock->min_intr_interval = next_edge - uart_now;
		}
	}
}

/*
 * UART over TCP (erbium_uart_tcp.c): received bytes enter the RX FIFO one
 * per period like the file input; transmitted bytes are sent every
 * UART_TCP_FLUSH_CLOCKS (about 10 us at 200 MHz).
 */
#define UART_TCP_FLUSH_CLOCKS (UART_CHARACTER_CLOCKS * 67U)
static uint64 tcp_next_flush_clock;

static void exchange_with_tcp(DeviceClockType *clock)
{
	uint8 data;
	uint64 until;

	if (((uart_now % UART_CHARACTER_CLOCKS) == 0U) && (rx_count < UART_FIFO_SIZE)
		&& erbium_uart_tcp_get(&data)) {
		(void)erbium_uart_receive(data);
	}
	if (uart_now >= tcp_next_flush_clock) {
		erbium_uart_tcp_flush();
		tcp_next_flush_clock = uart_now + UART_TCP_FLUSH_CLOCKS;
	}
	/* A clock skip may not jump over the next byte or the next flush. */
	until = tcp_next_flush_clock - uart_now;
	if (erbium_uart_tcp_pending() && (rx_count < UART_FIFO_SIZE)) {
		until = UART_CHARACTER_CLOCKS - (uart_now % UART_CHARACTER_CLOCKS);
	}
	if (until < clock->min_intr_interval) {
		clock->min_intr_interval = until;
	}
}

void erbium_uart_tick(DeviceClockType *clock)
{
	uint64 until;

	uart_now = clock->clock;
	if (rx_file != NULL) {
		receive_from_file(clock);
	}
	if (erbium_uart_tcp_enabled()) {
		exchange_with_tcp(clock);
	}
	if (ie == 0U) {
		return;
	}
	update_irq();
	/* Do not skip clocks past the drain while the TX-empty interrupt is on. */
	if (((ie & UART_STATUS_TX_EMPTY) != 0U) && (tx_count() != 0U)) {
		until = tx_empty_clock - uart_now;
		if (until < clock->min_intr_interval) {
			clock->min_intr_interval = until;
		}
	}
}
