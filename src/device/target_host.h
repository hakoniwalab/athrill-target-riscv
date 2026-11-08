/*
 * The RISC-V target's implementation of the athrill-device required
 * interface (AthrillDeviceTargetOps), used by device_tree.c.
 *
 * Copyright 2026 Hakoniwa Laboratory
 * SPDX-License-Identifier: MIT
 */
#ifndef ATHRILL_RISCV_TARGET_HOST_H
#define ATHRILL_RISCV_TARGET_HOST_H

#include "athrill_device_if.h"
#include "std_errno.h"
#include "std_types.h"

sint32 riscv_host_mmio_register(void *context, uint64 address, uint64 size,
	const AthrillDeviceMmioOps *ops, void *instance, AthrillDeviceMmioToken *token);
void riscv_host_mmio_unregister(void *context, AthrillDeviceMmioToken token);
/* TRUE when a model's registers cover the address (the target's own device there is replaced). */
std_bool riscv_host_mmio_covers(uint64 address);
sint32 riscv_host_irq_resolve(void *context, const AthrillDeviceSpecifier *spec,
	AthrillDeviceIrqLine *line);
sint32 riscv_host_irq_set(void *context, AthrillDeviceIrqLine line, std_bool asserted);
std_bool riscv_host_irq_is_active(void *context, AthrillDeviceIrqLine line);
uint64 riscv_host_time_now(void *context);
uint64 riscv_host_time_frequency_hz(void *context);
sint32 riscv_host_clock_frequency_hz(void *context, AthrillDeviceNodeId node,
	const char *clock_name, uint64 *frequency);
sint32 riscv_host_memory_read(void *context, uint64 address, void *data, uint32 size);
sint32 riscv_host_memory_write(void *context, uint64 address, const void *data, uint32 size);
Std_ReturnType riscv_host_config_string(const char *key, char **value);
sint32 riscv_host_value(void *context, const char *key, const char **value);
sint32 riscv_host_console_write(void *context, uint32 channel, uint8 data);
void riscv_host_end_batch(void *context);
void riscv_host_reset_values(void);

#endif
