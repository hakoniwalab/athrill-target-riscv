/*
 * athrill-device target adapter for the RISC-V target, after the STM32
 * target's (athrill-target-stm32 src/device/device_tree.c).
 *
 * Builds the Devicetree runtime (athrill_device_build) and hands it the
 * AthrillDeviceTargetOps table: the REQUIRED operations in target_host.c,
 * the TO BE REMOVED bridges all NULL (models use the runtime's resources).
 *
 * Copyright 2026 Hakoniwa Laboratory
 * SPDX-License-Identifier: MIT
 */
#include "device_tree.h"

#include "athrill_device_if.h"
#include "cpuemu_ops.h"
#include "std_errno.h"
#include "target_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PLUGINS 32U
#define MAX_HOST_VALUES 32U
#define MAX_PLUGIN_MAPPING 1024U

typedef struct {
	uint32 unused;
} RiscvDeviceTreeTarget;

static RiscvDeviceTreeTarget target_context;
static AthrillDeviceRuntime *device_tree_runtime;
static std_bool destroy_registered;

static char plugin_mappings[MAX_PLUGINS][MAX_PLUGIN_MAPPING];

static const AthrillDeviceTargetOps target_ops = {
	ATHRILL_DEVICE_ABI_MAJOR, ATHRILL_DEVICE_ABI_MINOR, sizeof(AthrillDeviceTargetOps),
	/* REQUIRED operations: target_host.c */
	riscv_host_mmio_register, riscv_host_mmio_unregister, riscv_host_irq_resolve, riscv_host_irq_set,
	riscv_host_irq_is_active, riscv_host_time_now, riscv_host_time_frequency_hz,
	riscv_host_clock_frequency_hz, riscv_host_memory_read, riscv_host_memory_write,
	/* TO BE REMOVED bridge operations: all NULL (the layout of ABI 1.x) */
	NULL, NULL, NULL,	/* spi_bind, spi_set_chip_select, spi_exchange_byte */
	NULL, NULL,	/* i2c_bind, i2c_transfer */
	NULL, NULL, NULL,	/* uart_bind, uart_peripheral, uart_controller */
	NULL, NULL, NULL, NULL,	/* gpio_resolve, gpio_read, gpio_drive, gpio_subscribe */
	NULL, NULL,	/* dma_resolve, dma_request */
	NULL /* model_adapter_connect */, NULL /* spi_bind_node */, riscv_host_value,
	NULL /* sensor_sample */, NULL /* i2c_bind_node */, NULL /* uart_bind_nodes */,
	NULL /* uart_send_from_node */, NULL /* gps_sample */, NULL /* actuator_output */,
	NULL,	/* gpio_outputs */
	/* ABI 1.10 REQUIRED operations: target_host.c */
	riscv_host_console_write, riscv_host_end_batch
};

/* Host-only values the Erbium models read (logged with the DTB's hash). */
static uint32 collect_host_values(AthrillDeviceHostValue values[MAX_HOST_VALUES])
{
	static const char *const keys[] = {
		"DEVICE_CONFIG_ERBIUM_UART_RX_FILE", "DEVICE_CONFIG_ERBIUM_UART_TCP_PORT"
	};
	uint32 count = 0U, i;
	const char *value;
	for (i = 0U; i < sizeof(keys) / sizeof(keys[0]) && count < MAX_HOST_VALUES; i++) {
		value = NULL;
		if (riscv_host_value(NULL, keys[i], &value) == 0) {
			values[count].key = keys[i];
			values[count].value = value;
			count++;
		}
	}
	return count;
}

static int collect_plugins(AthrillDevicePluginPath plugins[MAX_PLUGINS])
{
	uint32 i;
	char key[64];
	char *mapping;
	for (i = 0U; i < MAX_PLUGINS; i++) {
		char *equals;
		char *encoded;
		snprintf(key, sizeof(key), "DEVICE_CONFIG_DEVICE_PLUGIN_%u", i);
		mapping = NULL;
		if (riscv_host_config_string(key, &mapping) != STD_E_OK) {
			break;
		}
		if (strlen(mapping) >= sizeof(plugin_mappings[i])) {
			return -1;
		}
		strcpy(plugin_mappings[i], mapping);
		mapping = plugin_mappings[i];
		equals = strchr(mapping, '=');
		if (equals == NULL || equals == mapping || equals[1] == 0) {
			return -1;
		}
		*equals = 0;
		/* device_config.txt values cannot hold ','; it is written %2C */
		while ((encoded = strstr(mapping, "%2C")) != NULL ||
		       (encoded = strstr(mapping, "%2c")) != NULL) {
			*encoded = ',';
			memmove(encoded + 1, encoded + 3, strlen(encoded + 3) + 1U);
		}
		plugins[i].compatible = mapping;
		plugins[i].library_path = equals + 1;
	}
	return (int)i;
}

std_bool riscv_device_tree_build(void)
{
	/* The hart, its local interrupt controller and the PLIC are the target's. */
	static const char *const target_compatibles[] = {
		"riscv", "riscv,cpu-intc", "sifive,plic-1.0.0"
	};
	AthrillDeviceBuildOptions options;
	AthrillDevicePluginPath plugins[MAX_PLUGINS];
	AthrillDeviceHostValue host_values[MAX_HOST_VALUES];
	char *path = NULL;
	int plugin_count;
	riscv_device_tree_destroy();
	riscv_host_reset_values();
	if (riscv_host_config_string("DEVICE_CONFIG_DEVICE_TREE", &path) != STD_E_OK) {
		return FALSE;
	}
	plugin_count = collect_plugins(plugins);
	if (plugin_count < 0) {
		printf("DEVICE_TREE_ERROR code=plugin-config node=/ property=DEVICE_CONFIG_DEVICE_PLUGIN reason=expected-compatible=path\n");
		exit(1);
	}
	memset(&options, 0, sizeof(options));
	options.dtb_path = path;
	options.plugins = plugins;
	options.plugin_count = (uint32)plugin_count;
	options.target_compatibles = target_compatibles;
	options.target_compatible_count = sizeof(target_compatibles) / sizeof(target_compatibles[0]);
	options.maximum_dtb_size = 16U * 1024U * 1024U;
	options.host_values = host_values;
	options.host_value_count = collect_host_values(host_values);
	memset(&target_context, 0, sizeof(target_context));
	if (athrill_device_build(&options, &target_ops, &target_context, &device_tree_runtime) != 0) {
		printf("ERROR: DEVICE_CONFIG_DEVICE_TREE build failed\n");
		exit(1);
	}
	if (!destroy_registered) {
		if (atexit(riscv_device_tree_destroy) != 0) {
			printf("ERROR: can not register device tree cleanup\n");
			exit(1);
		}
		destroy_registered = TRUE;
	}
	return TRUE;
}

void riscv_device_tree_tick(DeviceClockType *clock)
{
	AthrillDeviceTickResult result;
	if (device_tree_runtime == NULL) {
		return;
	}
	if (athrill_device_tick(device_tree_runtime, clock->clock, &result) != 0) {
		printf("ERROR: device tree tick failed\n");
		exit(1);
	}
	if (result.next_event_interval < clock->min_intr_interval) {
		clock->min_intr_interval = result.next_event_interval;
	}
	if (!result.can_skip) {
		clock->can_skip_clock = FALSE;
	}
}

void riscv_device_tree_destroy(void)
{
	if (device_tree_runtime != NULL) {
		athrill_device_destroy(device_tree_runtime);
		device_tree_runtime = NULL;
	}
}

std_bool riscv_device_tree_console_input(uint8 data)
{
	return (device_tree_runtime != NULL &&
		athrill_device_console_input(device_tree_runtime, 0U, data) == 0) ? TRUE : FALSE;
}
