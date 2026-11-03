#ifndef HOST_MEMORY_STUB_H
#define HOST_MEMORY_STUB_H

/*
 * A flat little-endian memory at address 0 that stands in for Athrill's
 * MPU in host unit tests linked with the CPU sources. The including file
 * defines HOST_MEMORY_SIZE before including this header.
 */
#include "mpu_ops.h"
#include "std_errno.h"

static uint8 memory[HOST_MEMORY_SIZE];

int cpu_config_get_core_id_num(void)
{
	return 1;
}

Std_ReturnType mpu_get_pointer(CoreIdType id, AthrillAddrType address, uint8 **pointer)
{
	(void)id;
	if (address >= sizeof(memory)) {
		return STD_E_INVALID;
	}
	*pointer = &memory[address];
	return STD_E_OK;
}

static Std_ReturnType read_bytes(CoreIdType id, AthrillAddrType address, uint32 size, uint32 *value)
{
	uint8 *pointer;
	uint32 index;

	*value = 0U;
	for (index = 0U; index < size; index++) {
		if (mpu_get_pointer(id, address + index, &pointer) != STD_E_OK) {
			return STD_E_INVALID;
		}
		*value |= (uint32)*pointer << (8U * index);
	}
	return STD_E_OK;
}

static Std_ReturnType write_bytes(CoreIdType id, AthrillAddrType address, uint32 size, uint32 value)
{
	uint8 *pointer;
	uint32 index;

	for (index = 0U; index < size; index++) {
		if (mpu_get_pointer(id, address + index, &pointer) != STD_E_OK) {
			return STD_E_INVALID;
		}
		*pointer = (uint8)(value >> (8U * index));
	}
	return STD_E_OK;
}

Std_ReturnType mpu_get_data8(CoreIdType id, AthrillAddrType address, uint8 *data)
{
	uint32 value;
	Std_ReturnType result = read_bytes(id, address, 1U, &value);

	*data = (uint8)value;
	return result;
}

Std_ReturnType mpu_get_data16(CoreIdType id, AthrillAddrType address, uint16 *data)
{
	uint32 value;
	Std_ReturnType result = read_bytes(id, address, 2U, &value);

	*data = (uint16)value;
	return result;
}

Std_ReturnType mpu_get_data32(CoreIdType id, AthrillAddrType address, uint32 *data)
{
	return read_bytes(id, address, 4U, data);
}

Std_ReturnType mpu_put_data8(CoreIdType id, AthrillAddrType address, uint8 data)
{
	return write_bytes(id, address, 1U, data);
}

Std_ReturnType mpu_put_data16(CoreIdType id, AthrillAddrType address, uint16 data)
{
	return write_bytes(id, address, 2U, data);
}

Std_ReturnType mpu_put_data32(CoreIdType id, AthrillAddrType address, uint32 data)
{
	return write_bytes(id, address, 4U, data);
}

#endif /* HOST_MEMORY_STUB_H */
