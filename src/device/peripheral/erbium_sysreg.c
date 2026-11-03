/*
 * Erbium system-register map (base 0x02000000):
 *   0x00 platform identifier, 0x08 platform revision
 *   other offsets are byte-addressable backing storage
 */
#include "erbium_devices.h"

#include <string.h>

#define SYSREG_BASE 0x02000000ULL

uint8 erbium_sysreg_data[0x1000];

static Std_ReturnType get8(
    MpuAddressRegionType *region,
    CoreIdType core_id,
    AthrillAddrType address,
    uint8 *data)
{
	riscv_device_accessed = TRUE;
	(void)region;
	(void)core_id;
	*data = erbium_sysreg_data[address - SYSREG_BASE];
	return STD_E_OK;
}

static Std_ReturnType get16(
    MpuAddressRegionType *region,
    CoreIdType core_id,
    AthrillAddrType address,
    uint16 *data)
{
	riscv_device_accessed = TRUE;
	uint8 low;
	uint8 high;

	(void)get8(region, core_id, address, &low);
	(void)get8(region, core_id, address + 1U, &high);
	*data = (uint16)low | ((uint16)high << 8U);
	return STD_E_OK;
}

static Std_ReturnType get32(
    MpuAddressRegionType *region,
    CoreIdType core_id,
    AthrillAddrType address,
    uint32 *data)
{
	riscv_device_accessed = TRUE;
	uint16 low;
	uint16 high;

	(void)get16(region, core_id, address, &low);
	(void)get16(region, core_id, address + 2U, &high);
	*data = (uint32)low | ((uint32)high << 16U);
	return STD_E_OK;
}

static Std_ReturnType put8(
    MpuAddressRegionType *region,
    CoreIdType core_id,
    AthrillAddrType address,
    uint8 data)
{
	riscv_device_accessed = TRUE;
	(void)region;
	(void)core_id;
	erbium_sysreg_data[address - SYSREG_BASE] = data;
	return STD_E_OK;
}

static Std_ReturnType put16(
    MpuAddressRegionType *region,
    CoreIdType core_id,
    AthrillAddrType address,
    uint16 data)
{
	riscv_device_accessed = TRUE;
	(void)put8(region, core_id, address, (uint8)data);
	(void)put8(region, core_id, address + 1U, (uint8)(data >> 8U));
	return STD_E_OK;
}

static Std_ReturnType put32(
    MpuAddressRegionType *region,
    CoreIdType core_id,
    AthrillAddrType address,
    uint32 data)
{
	riscv_device_accessed = TRUE;
	(void)put16(region, core_id, address, (uint16)data);
	(void)put16(region, core_id, address + 2U, (uint16)(data >> 16U));
	return STD_E_OK;
}

MpuAddressRegionOperationType erbium_sysreg_ops = {
	get8,
	get16,
	get32,
	put8,
	put16,
	put32,
	NULL
};

void erbium_sysreg_reset(void)
{
	memset(erbium_sysreg_data, 0, sizeof(erbium_sysreg_data));
	(void)put32(NULL, 0U, SYSREG_BASE, 0xeb680000U);
	(void)put32(NULL, 0U, SYSREG_BASE + 8U, 0x14U);
}
