/*
 * See target_host.h: the RISC-V target's implementation of the athrill-device
 * required interface, after the STM32 target's (athrill-target-stm32
 * src/device/target_host.c) with 64-bit addresses.
 *
 * Every access to a model's registers ends the CPU batch, as every access to
 * the target's own device registers does (mmio_register.h): the target's
 * rule, the same for every model.
 *
 * Interrupt specifiers: <&plic source flags> is a PLIC source (the PLIC is
 * target-owned); <&hlic n> is the hart's local interrupt n (a mip bit).
 *
 * Copyright 2026 Hakoniwa Laboratory
 * SPDX-License-Identifier: MIT
 */
#include "target_host.h"

#include "bus.h"
#include "concrete_executor/target/dbg_target_serial.h"
#include "cpuemu_ops.h"
#include "intc.h"
#include "mmio_register.h"
#include "mpu.h"
#include "target_cpu.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_MMIO_ADAPTERS 64U
#define MAX_HOST_VALUES 32U
#define DEFAULT_CPU_FREQUENCY_MHZ 200U

/* An interrupt line: a PLIC source, or with this bit a mip bit. */
#define IRQ_LOCAL 0x100000000ULL

typedef struct {
	const AthrillDeviceMmioOps *ops;
	void *instance;
	uint32 region_index;
	std_bool active;
} MmioAdapter;

static MmioAdapter mmio[MAX_MMIO_ADAPTERS];

typedef struct {
	char key[64];
	char value[32];
} NumericHostValue;
static NumericHostValue numeric_host_values[MAX_HOST_VALUES];
static uint32 numeric_host_value_count;

static MmioAdapter *mmio_of(MpuAddressRegionType *region)
{
	return (MmioAdapter *)region->data;
}

static Std_ReturnType mmio_get(MpuAddressRegionType *region, AthrillAddrType address,
                               uint32 size, uint64 *value)
{
	MmioAdapter *adapter = mmio_of(region);
	riscv_device_accessed = TRUE;
	if (!adapter->active || adapter->ops->read(adapter->instance, address, size, value) != 0) {
		return STD_E_INVALID;
	}
	return STD_E_OK;
}
static Std_ReturnType mmio_put(MpuAddressRegionType *region, AthrillAddrType address,
                               uint32 size, uint64 value)
{
	MmioAdapter *adapter = mmio_of(region);
	riscv_device_accessed = TRUE;
	return (!adapter->active ||
	        adapter->ops->write(adapter->instance, address, size, value) != 0) ? STD_E_INVALID : STD_E_OK;
}
static Std_ReturnType mmio_get8(MpuAddressRegionType *region, CoreIdType core,
                                AthrillAddrType address, uint8 *data)
{
	uint64 value = 0U;
	Std_ReturnType result = mmio_get(region, address, 1U, &value);
	(void)core;
	*data = (uint8)value;
	return result;
}
static Std_ReturnType mmio_get16(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint16 *data)
{
	uint64 value = 0U;
	Std_ReturnType result = mmio_get(region, address, 2U, &value);
	(void)core;
	*data = (uint16)value;
	return result;
}
static Std_ReturnType mmio_get32(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint32 *data)
{
	uint64 value = 0U;
	Std_ReturnType result = mmio_get(region, address, 4U, &value);
	(void)core;
	*data = (uint32)value;
	return result;
}
static Std_ReturnType mmio_put8(MpuAddressRegionType *region, CoreIdType core,
                                AthrillAddrType address, uint8 data)
{
	(void)core;
	return mmio_put(region, address, 1U, data);
}
static Std_ReturnType mmio_put16(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint16 data)
{
	(void)core;
	return mmio_put(region, address, 2U, data);
}
static Std_ReturnType mmio_put32(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint32 data)
{
	(void)core;
	return mmio_put(region, address, 4U, data);
}
static Std_ReturnType mmio_pointer(MpuAddressRegionType *region, CoreIdType core,
                                   AthrillAddrType address, uint8 **data)
{
	(void)region;
	(void)core;
	(void)address;
	(void)data;
	return STD_E_INVALID;
}
static MpuAddressRegionOperationType mmio_region_ops = {
	mmio_get8, mmio_get16, mmio_get32, mmio_put8, mmio_put16, mmio_put32, mmio_pointer
};

sint32 riscv_host_mmio_register(void *context, uint64 address, uint64 size,
                                const AthrillDeviceMmioOps *ops, void *instance,
                                AthrillDeviceMmioToken *token)
{
	uint32 i;
	MpuAddressRegionType *maps;
	(void)context;
	if (size == 0U || address + size < address) {
		return -1;
	}
	for (i = 0U; i < MAX_MMIO_ADAPTERS; i++) {
		if (!mmio[i].active) {
			break;
		}
	}
	if (i == MAX_MMIO_ADAPTERS) {
		return -1;
	}
	mmio[i].ops = ops;
	mmio[i].instance = instance;
	mmio[i].active = TRUE;
	maps = realloc(mpu_address_map.dynamic_map, sizeof(*maps) * (mpu_address_map.dynamic_map_num + 1U));
	if (maps == NULL) {
		mmio[i].active = FALSE;
		return -1;
	}
	mpu_address_map.dynamic_map = maps;
	mmio[i].region_index = mpu_address_map.dynamic_map_num;
	maps[mmio[i].region_index].type = DEVICE;
	maps[mmio[i].region_index].is_malloc = FALSE;
	maps[mmio[i].region_index].permission = MPU_ADDRESS_REGION_PERM_ALL;
	maps[mmio[i].region_index].start = (AthrillAddrType)address;
	maps[mmio[i].region_index].size = (AthrillAddrType)size;
	maps[mmio[i].region_index].mask = MPU_ADDRESS_REGION_MASK_ALL;
	maps[mmio[i].region_index].data = (uint8 *)&mmio[i];
	maps[mmio[i].region_index].ops = &mmio_region_ops;
	mpu_address_map.dynamic_map_num++;
	token->value = (uint64)i + 1U;
	return 0;
}

void riscv_host_mmio_unregister(void *context, AthrillDeviceMmioToken token)
{
	uint64 i = token.value;
	(void)context;
	if (i == 0U || i > MAX_MMIO_ADAPTERS) {
		return;
	}
	i--;
	if (mmio[i].active) {
		mmio[i].active = FALSE;
		if (mmio[i].region_index < mpu_address_map.dynamic_map_num) {
			mpu_address_map.dynamic_map[mmio[i].region_index].size = 0U;
		}
	}
}

std_bool riscv_host_mmio_covers(uint64 address)
{
	uint32 i;
	for (i = 0U; i < MAX_MMIO_ADAPTERS; i++) {
		if (mmio[i].active && mmio[i].region_index < mpu_address_map.dynamic_map_num) {
			const MpuAddressRegionType *r = &mpu_address_map.dynamic_map[mmio[i].region_index];
			if (address >= r->start && address - r->start < r->size) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

sint32 riscv_host_irq_resolve(void *context, const AthrillDeviceSpecifier *spec,
                              AthrillDeviceIrqLine *line)
{
	(void)context;
	if (spec == NULL || line == NULL || spec->cell_count == 0U || spec->provider_compatible == NULL) {
		return -1;
	}
	if (strcmp(spec->provider_compatible, "sifive,plic-1.0.0") == 0) {
		if (spec->cells[0] < INTC_PLIC_SOURCE_MIN || spec->cells[0] > INTC_PLIC_SOURCE_MAX) {
			return -1;
		}
		line->value = spec->cells[0];
		return 0;
	}
	if (strcmp(spec->provider_compatible, "riscv,cpu-intc") == 0) {
		if (spec->cells[0] >= 64U) {
			return -1;
		}
		line->value = IRQ_LOCAL | spec->cells[0];
		return 0;
	}
	return -1;
}
sint32 riscv_host_irq_set(void *context, AthrillDeviceIrqLine line, std_bool asserted)
{
	(void)context;
	if ((line.value & IRQ_LOCAL) != 0U) {
		intc_set_local_interrupt(0U, (uint32)(line.value & 0x3fU), asserted);
		return 0;
	}
	if (line.value < INTC_PLIC_SOURCE_MIN || line.value > INTC_PLIC_SOURCE_MAX) {
		return -1;
	}
	intc_plic_set_source((uint32)line.value, asserted);
	return 0;
}
std_bool riscv_host_irq_is_active(void *context, AthrillDeviceIrqLine line)
{
	(void)context;
	if ((line.value & IRQ_LOCAL) != 0U) {
		return ((virtual_cpu.cores[0].core.reg.csr.mip >> (line.value & 0x3fU)) & 1U) != 0U ? TRUE : FALSE;
	}
	return intc_plic_source_asserted((uint32)line.value);
}
uint64 riscv_host_time_now(void *context)
{
	(void)context;
	return cpuemu_get_clock();
}
uint64 riscv_host_time_frequency_hz(void *context)
{
	uint32 mhz = DEFAULT_CPU_FREQUENCY_MHZ;
	(void)context;
	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &mhz);
	return (uint64)mhz * 1000000U;
}
sint32 riscv_host_clock_frequency_hz(void *context, AthrillDeviceNodeId node,
                                     const char *clock_name, uint64 *frequency)
{
	(void)context;
	(void)node;
	(void)clock_name;
	*frequency = riscv_host_time_frequency_hz(NULL);
	return 0;
}

sint32 riscv_host_memory_read(void *context, uint64 address, void *data, uint32 size)
{
	uint32 i;
	(void)context;
	/* 1, 2 or 4 bytes: one access of that width; a larger size byte by byte. */
	if (size == 2U) {
		uint16 v16;
		if (bus_get_data16(0U, (AthrillAddrType)address, &v16) != STD_E_OK) {
			return -1;
		}
		memcpy(data, &v16, 2U);
		return 0;
	}
	if (size == 4U) {
		uint32 v32;
		if (bus_get_data32(0U, (AthrillAddrType)address, &v32) != STD_E_OK) {
			return -1;
		}
		memcpy(data, &v32, 4U);
		return 0;
	}
	for (i = 0U; i < size; i++) {
		if (bus_get_data8(0U, (AthrillAddrType)(address + i), &((uint8 *)data)[i]) != STD_E_OK) {
			return -1;
		}
	}
	return 0;
}
sint32 riscv_host_memory_write(void *context, uint64 address, const void *data, uint32 size)
{
	uint32 i;
	(void)context;
	if (size == 2U) {
		uint16 v16;
		memcpy(&v16, data, 2U);
		return (bus_put_data16(0U, (AthrillAddrType)address, v16) == STD_E_OK) ? 0 : -1;
	}
	if (size == 4U) {
		uint32 v32;
		memcpy(&v32, data, 4U);
		return (bus_put_data32(0U, (AthrillAddrType)address, v32) == STD_E_OK) ? 0 : -1;
	}
	for (i = 0U; i < size; i++) {
		if (bus_put_data8(0U, (AthrillAddrType)(address + i), ((const uint8 *)data)[i]) != STD_E_OK) {
			return -1;
		}
	}
	return 0;
}

/* A device configuration string, without the lookup's echo on stdout. */
Std_ReturnType riscv_host_config_string(const char *key, char **value)
{
	int saved_stdout, null_fd;
	Std_ReturnType result;
	(void)fflush(stdout);
	saved_stdout = dup(STDOUT_FILENO);
	null_fd = open("/dev/null", O_WRONLY);
	if (saved_stdout < 0 || null_fd < 0 || dup2(null_fd, STDOUT_FILENO) < 0) {
		if (saved_stdout >= 0) {
			close(saved_stdout);
		}
		if (null_fd >= 0) {
			close(null_fd);
		}
		return cpuemu_get_devcfg_string(key, value);
	}
	result = cpuemu_get_devcfg_string(key, value);
	(void)fflush(stdout);
	(void)dup2(saved_stdout, STDOUT_FILENO);
	(void)close(saved_stdout);
	(void)close(null_fd);
	return result;
}

sint32 riscv_host_value(void *context, const char *key, const char **value)
{
	char *configured = NULL;
	uint32 number;
	uint32 i;
	(void)context;
	if (key == NULL || value == NULL) {
		return -1;
	}
	if (riscv_host_config_string(key, &configured) == STD_E_OK) {
		*value = configured;
		return 0;
	}
	for (i = 0U; i < numeric_host_value_count; i++) {
		if (strcmp(numeric_host_values[i].key, key) == 0) {
			*value = numeric_host_values[i].value;
			return 0;
		}
	}
	if ((cpuemu_get_devcfg_value(key, &number) != STD_E_OK &&
	     cpuemu_get_devcfg_value_hex(key, &number) != STD_E_OK) ||
	    numeric_host_value_count >= MAX_HOST_VALUES ||
	    strlen(key) >= sizeof(numeric_host_values[0].key)) {
		return -1;
	}
	strcpy(numeric_host_values[numeric_host_value_count].key, key);
	snprintf(numeric_host_values[numeric_host_value_count].value,
		sizeof(numeric_host_values[0].value), "%u", number);
	*value = numeric_host_values[numeric_host_value_count].value;
	numeric_host_value_count++;
	return 0;
}

sint32 riscv_host_console_write(void *context, uint32 channel, uint8 data)
{
	(void)context;
	if (channel > 0xFFU) {
		return -1;
	}
	return dbg_serial_putchar((uint8)channel, data) ? 0 : -1;
}

void riscv_host_end_batch(void *context)
{
	(void)context;
	riscv_device_accessed = TRUE;
}

void riscv_host_reset_values(void)
{
	numeric_host_value_count = 0U;
}
