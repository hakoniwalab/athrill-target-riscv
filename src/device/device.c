#include "cpu.h"
#include "std_device_ops.h"
#include "peripheral/erbium_devices.h"
void erbium_uart_reset(void); void erbium_timer_reset(void); void erbium_timer_tick(DeviceClockType*); void erbium_plic_reset(void);

void erbium_devices_reset(void){erbium_uart_reset();erbium_timer_reset();erbium_plic_reset();}
void device_init(CpuType *cpu, DeviceClockType *clock) { (void)cpu; clock->clock=0; clock->intclock=0; clock->min_intr_interval=DEVICE_CLOCK_MAX_INTERVAL; clock->can_skip_clock=TRUE; clock->is_halt=FALSE; erbium_devices_reset(); }
void device_reset(CpuType *cpu, DeviceClockType *clock) { device_init(cpu, clock); }
void device_supply_clock(DeviceClockType *clock) { clock->can_skip_clock=TRUE; clock->min_intr_interval=DEVICE_CLOCK_MAX_INTERVAL; erbium_timer_tick(clock); }
void erbium_devices_supply_clock(DeviceClockType *clock){erbium_timer_tick(clock);}
void device_get_clock(DeviceClockType *clock) { (void)clock; }
int intc_raise_intr(uint32 intno) { (void)intno; return 0; }
void device_raise_int(uint16 intno) { (void)intno; }
