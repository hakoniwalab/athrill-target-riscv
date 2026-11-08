/*
 * The RISC-V target's devices: the PLIC and the hart's local interrupts are
 * the target's (intc); the board's devices come only from the Devicetree
 * (DEVICE_CONFIG_DEVICE_TREE, athrill-device boards/erbium/erbium.dtb):
 * Erbium's UART, machine timer and register blocks are athrill-device
 * models. Without a Devicetree Athrill runs the hart with no board (the
 * bare-metal selftests).
 */
#include "cpu.h"
#include "cpuemu_ops.h"
#include "intc.h"
#include "device_access.h"
#include "std_device_ops.h"
#include "device_tree.h"
#ifdef EXDEV_ENABLE
#include "athrill_device.h"
#endif

extern uint32 riscv_trace_enabled;

std_bool riscv_device_accessed = FALSE;
#ifdef EXDEV_ENABLE
static std_bool exdev_initialized = FALSE;
#endif

void device_init(CpuType *cpu, DeviceClockType *clock)
{
	char *rx_file = NULL;

	(void)cpu;
	(void)cpuemu_get_devcfg_value("DEBUG_FUNC_RISCV_TRACE",
	                              &riscv_trace_enabled);
	clock->clock = 0U;
	clock->intclock = 0U;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	clock->can_skip_clock = TRUE;
	clock->is_halt = FALSE;
	intc_reset();
	(void)riscv_device_tree_build();
	/* The UART model reads its input file as a host value (without an
	 * echo); this lookup keeps the one echo line the console always had. */
	(void)cpuemu_get_devcfg_string("DEVICE_CONFIG_ERBIUM_UART_RX_FILE", &rx_file);
}

void device_reset(CpuType *cpu, DeviceClockType *clock)
{
	device_init(cpu, clock);
}

void device_supply_clock(DeviceClockType *clock)
{
	clock->can_skip_clock = TRUE;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	riscv_device_tree_tick(clock);
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
