/*
 * The board from the Devicetree (DEVICE_CONFIG_DEVICE_TREE): the
 * athrill-device runtime and its models.
 *
 * Copyright 2026 Hakoniwa Laboratory
 * SPDX-License-Identifier: MIT
 */
#ifndef ATHRILL_RISCV_DEVICE_TREE_H
#define ATHRILL_RISCV_DEVICE_TREE_H

#include "cpu.h"
#include "std_device_ops.h"

/* Build the runtime from DEVICE_CONFIG_DEVICE_TREE; FALSE when the key is absent. */
std_bool riscv_device_tree_build(void);
void riscv_device_tree_tick(DeviceClockType *clock);
void riscv_device_tree_destroy(void);
/* Host console input for a model listening on the channel (ABI 1.17). */
std_bool riscv_device_tree_console_input(uint8 data);

#endif
