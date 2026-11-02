#ifndef ATHRILL_RISCV_MPU_CONFIG_H
#define ATHRILL_RISCV_MPU_CONFIG_H
#include "std_types.h"
#include "loader/elf.h"
#define MPU_CONFIG_REGION_NUM UINT_C(4)
#define MPU_ADDRESS_REGION_INX_SERIAL 0U
#define ELF_MACHINE_TYPE EM_RISCV
#endif
