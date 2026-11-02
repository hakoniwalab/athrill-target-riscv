#ifndef ATHRILL_ERBIUM_DEVICES_H
#define ATHRILL_ERBIUM_DEVICES_H
#include "cpu.h"
#include "std_errno.h"
#include "mpu_types.h"
#include "std_device_ops.h"
#include "intc.h"

extern MpuAddressRegionOperationType erbium_sysreg_ops;
extern MpuAddressRegionOperationType erbium_uart_ops;
extern MpuAddressRegionOperationType erbium_timer_ops;
extern uint8 erbium_sysreg_data[0x1000];
extern uint8 erbium_uart_data[0x100];
extern uint8 erbium_timer_data[0x10];
void erbium_devices_reset(void);
void erbium_devices_supply_clock(DeviceClockType *clock);
void erbium_sysreg_reset(void);
void erbium_uart_reset(void);
void erbium_uart_tick(DeviceClockType *clock);
void erbium_timer_reset(void);
void erbium_timer_tick(DeviceClockType *clock);
Std_ReturnType erbium_uart_receive(uint8 data);
void erbium_timer_configure(uint32 cpu_frequency, uint32 timer_frequency);
#endif
