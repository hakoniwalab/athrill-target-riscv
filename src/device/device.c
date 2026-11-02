#include "cpu.h"
#include "std_device_ops.h"

void device_init(CpuType *cpu, DeviceClockType *clock) { (void)cpu; clock->clock=0; clock->intclock=0; clock->min_intr_interval=DEVICE_CLOCK_MAX_INTERVAL; clock->can_skip_clock=TRUE; clock->is_halt=FALSE; }
void device_reset(CpuType *cpu, DeviceClockType *clock) { device_init(cpu, clock); }
void device_supply_clock(DeviceClockType *clock) { clock->can_skip_clock=TRUE; clock->min_intr_interval=DEVICE_CLOCK_MAX_INTERVAL; }
void device_get_clock(DeviceClockType *clock) { (void)clock; }
int intc_raise_intr(uint32 intno) { (void)intno; return 0; }
void device_raise_int(uint16 intno) { (void)intno; }
