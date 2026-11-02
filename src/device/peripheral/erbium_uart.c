/*
 * Erbium UART register map (base 0x02004000):
 *   0x00 baud, 0x08 transmit, 0x10 receive, 0x18 status
 *   0x28 control, 0x30 interrupt enable, 0x40 receive threshold
 */
#include "erbium_devices.h"
#include "concrete_executor/target/dbg_target_serial.h"
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

uint8 erbium_uart_data[0x100];
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
	(void)dbg_serial_putchar(0, data);
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
static Std_ReturnType g8(MpuAddressRegionType*r, CoreIdType c, AthrillAddrType a, uint8*d)
{
	uint32 v = read_reg(a & ~3ULL);
	(void)r;
	(void)c;
	*d = (uint8)(v >> ((a & 3U) * 8U));
	return STD_E_OK;
}
static Std_ReturnType g16(MpuAddressRegionType*r, CoreIdType c, AthrillAddrType a, uint16*d)
{
	uint8 x, y;
	g8(r, c, a, &x);
	g8(r, c, a + 1, &y);
	*d = (uint16)x | ((uint16)y << 8);
	return STD_E_OK;
}
static Std_ReturnType g32(MpuAddressRegionType*r, CoreIdType c, AthrillAddrType a, uint32*d)
{
	(void)r;
	(void)c;
	*d = read_reg(a);
	return STD_E_OK;
}
static Std_ReturnType p8(MpuAddressRegionType*r, CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)r;
	(void)c;
	write_reg(a, d);
	return STD_E_OK;
}
static Std_ReturnType p16(MpuAddressRegionType*r, CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)r;
	(void)c;
	write_reg(a, d);
	return STD_E_OK;
}
static Std_ReturnType p32(MpuAddressRegionType*r, CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)r;
	(void)c;
	write_reg(a, d);
	return STD_E_OK;
}
MpuAddressRegionOperationType erbium_uart_ops = {g8, g16, g32, p8, p16, p32, NULL};
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

void erbium_uart_tick(DeviceClockType *clock)
{
	uint64 until;

	uart_now = clock->clock;
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
