#include "cpu.h"
#include "cpuemu_ops.h"
#include "intc.h"
#include "peripheral/erbium_devices.h"
#include "std_device_ops.h"
#include "device_tree.h"
#include "target_host.h"
#ifdef EXDEV_ENABLE
#include "athrill_device.h"
#endif

/* Erbium: 200 MHz system clock, 2 MHz mtime (100 clocks per mtime tick). */
#define DEFAULT_CPU_FREQUENCY_MHZ 200U
#define DEFAULT_TIMER_FREQUENCY_HZ 2000000U

extern uint32 riscv_trace_enabled;

std_bool riscv_device_accessed = FALSE;
#ifdef EXDEV_ENABLE
static std_bool exdev_initialized = FALSE;
#endif

void erbium_devices_reset(void)
{
	erbium_sysreg_reset();
	erbium_uart_reset();
	erbium_timer_reset();
	intc_reset();
}

void device_init(CpuType *cpu, DeviceClockType *clock)
{
	uint32 cpu_frequency = DEFAULT_CPU_FREQUENCY_MHZ;
	uint32 timer_frequency = DEFAULT_TIMER_FREQUENCY_HZ;
	char *rx_file;
	uint32 tcp_port;

	(void)cpu;
	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &cpu_frequency);
	(void)cpuemu_get_devcfg_value("DEVICE_CONFIG_ERBIUM_TIMER_FREQ",
	                              &timer_frequency);
	(void)cpuemu_get_devcfg_value("DEBUG_FUNC_RISCV_TRACE",
	                              &riscv_trace_enabled);
	erbium_timer_configure(cpu_frequency, timer_frequency);
	clock->clock = 0U;
	clock->intclock = 0U;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	clock->can_skip_clock = TRUE;
	clock->is_halt = FALSE;
	erbium_devices_reset();
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_ERBIUM_UART_RX_FILE", &rx_file) == STD_E_OK) {
		erbium_uart_open_rx_file(rx_file);
	}
	else {
		erbium_uart_open_rx_file(NULL);
	}
	/* Listen once; a reset keeps the connection. */
	if (!erbium_uart_tcp_enabled()
		&& (cpuemu_get_devcfg_value("DEVICE_CONFIG_ERBIUM_UART_TCP_PORT", &tcp_port) == STD_E_OK)) {
		(void)erbium_uart_tcp_open(tcp_port);
	}
	/* The board's athrill-device models (DEVICE_CONFIG_DEVICE_TREE), if any:
	 * their registers come before the target's devices at the same address. */
	(void)riscv_device_tree_build();
}

void device_reset(CpuType *cpu, DeviceClockType *clock)
{
	device_init(cpu, clock);
}

/*
 * The target's devices tick unless an athrill-device model has replaced
 * them (its registers cover the device's base): UART at 0x02004000, machine
 * timer at 0x80f40200. Then the models tick, in the runtime's order.
 */
void erbium_devices_supply_clock(DeviceClockType *clock)
{
	if (!riscv_host_mmio_covers(0x02004000ULL)) {
		erbium_uart_tick(clock);
	}
	if (!riscv_host_mmio_covers(0x80f40200ULL)) {
		erbium_timer_tick(clock);
	}
	riscv_device_tree_tick(clock);
}

void device_supply_clock(DeviceClockType *clock)
{
	clock->can_skip_clock = TRUE;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	erbium_devices_supply_clock(clock);
#ifdef EXDEV_ENABLE
	/*
	 * External devices (memory.txt DEV) are registered when the program is
	 * loaded, after device_init(): they are initialised at the first tick,
	 * once (a reset keeps them). They may shorten min_intr_interval too.
	 */
	if (!exdev_initialized) {
		device_init_athrill_exdev();
		exdev_initialized = TRUE;
	}
	device_supply_clock_exdev(clock);
#endif
}

void device_get_clock(DeviceClockType *clock)
{
	(void)clock;
}

void device_raise_int(uint16 intno)
{
	(void)intc_raise_intr(intno);
}
