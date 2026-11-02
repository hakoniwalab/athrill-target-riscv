#ifndef ATHRILL_ERBIUM_DEVICES_H
#define ATHRILL_ERBIUM_DEVICES_H
#include "cpu.h"
#include "std_errno.h"
#include "mpu_types.h"
#include "std_device_ops.h"
extern MpuAddressRegionOperationType erbium_uart_ops, erbium_timer_ops, erbium_plic_ops;
extern uint8 erbium_uart_data[0x100], erbium_timer_data[0x10], erbium_plic_data[0x200008];
void erbium_devices_reset(void);
void erbium_devices_supply_clock(DeviceClockType *clock);
void erbium_plic_set_source(unsigned source, std_bool active);
#endif
