#include "cpu.h"
#include "cpuemu_ops.h"
#include "intc.h"
#include "peripheral/erbium_devices.h"
#include "std_device_ops.h"

#define DEFAULT_CPU_FREQUENCY_MHZ 100U
#define DEFAULT_TIMER_FREQUENCY_HZ 2000000U

extern uint32 riscv_trace_enabled;

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
}

void device_reset(CpuType *cpu, DeviceClockType *clock)
{
	device_init(cpu, clock);
}

void erbium_devices_supply_clock(DeviceClockType *clock)
{
	erbium_uart_tick();
	erbium_timer_tick(clock);
}

void device_supply_clock(DeviceClockType *clock)
{
	clock->can_skip_clock = TRUE;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	erbium_devices_supply_clock(clock);
}

void device_get_clock(DeviceClockType *clock)
{
	(void)clock;
}

void device_raise_int(uint16 intno)
{
	(void)intc_raise_intr(intno);
}
