/*
 * The flag every device register access sets (the target's own PLIC and
 * every athrill-device model): cpu_batch_can_continue() ends the CPU batch
 * on it, so devices see the clock of the access.
 */
#ifndef ATHRILL_RISCV_DEVICE_ACCESS_H
#define ATHRILL_RISCV_DEVICE_ACCESS_H

#include "std_types.h"

extern std_bool riscv_device_accessed;

#endif
