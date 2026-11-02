#include "cpu.h"
#include "std_device_ops.h"
#include "mpu_ops.h"
#include "std_errno.h"
#include "std_cpu_ops.h"
#include <stdio.h>
#include <string.h>
#include "peripheral/erbium_devices.h"
Std_ReturnType cpuemu_get_devcfg_value(const char *key, uint32 *value)
{
	(void)key;
	(void)value;
	return STD_E_INVALID;
}

static uint8 memory[256];
int cpu_config_get_core_id_num(void)
{
	return 1;
}
Std_ReturnType mpu_get_pointer(CoreIdType id, AthrillAddrType a, uint8 **p)
{
	(void)id;
	if (a >= sizeof(memory))return STD_E_INVALID;
	*p = &memory[a];
	return STD_E_OK;
}
Std_ReturnType mpu_get_data8(CoreIdType id, AthrillAddrType a, uint8*d)
{
	uint8*p;
	if (mpu_get_pointer(id, a, &p) != STD_E_OK)return STD_E_INVALID;
	*d = *p;
	return STD_E_OK;
}
Std_ReturnType mpu_get_data16(CoreIdType id, AthrillAddrType a, uint16*d)
{
	uint8*p;
	if (mpu_get_pointer(id, a, &p) != STD_E_OK)return STD_E_INVALID;
	*d = (uint16)p[0] | (uint16)p[1] << 8;
	return STD_E_OK;
}
Std_ReturnType mpu_get_data32(CoreIdType id, AthrillAddrType a, uint32*d)
{
	uint8*p;
	if (mpu_get_pointer(id, a, &p) != STD_E_OK)return STD_E_INVALID;
	*d = (uint32)p[0] | (uint32)p[1] << 8 | (uint32)p[2] << 16 | (uint32)p[3] << 24;
	return STD_E_OK;
}
Std_ReturnType mpu_put_data8(CoreIdType id, AthrillAddrType a, uint8 d)
{
	uint8*p;
	if (mpu_get_pointer(id, a, &p) != STD_E_OK)return STD_E_INVALID;
	*p = d;
	return STD_E_OK;
}
Std_ReturnType mpu_put_data16(CoreIdType id, AthrillAddrType a, uint16 d)
{
	return mpu_put_data8(id, a, (uint8)d) == STD_E_OK ? mpu_put_data8(id, a + 1,
	        (uint8)(d >> 8)) : STD_E_INVALID;
}
Std_ReturnType mpu_put_data32(CoreIdType id, AthrillAddrType a, uint32 d)
{
	unsigned n;
	for (n = 0; n < 4;
	     n++)if (mpu_put_data8(id, a + n, (uint8)(d >> (8 * n))) != STD_E_OK)return STD_E_INVALID;
	return STD_E_OK;
}

extern void device_init(CpuType *, DeviceClockType *);
extern void device_supply_clock(DeviceClockType *);
static void put_insn(unsigned a, uint32 v)
{
	memcpy(&memory[a], &v, 4);
}
int main(void)
{
	DeviceClockType clock;
	unsigned n;
	const uint32 addi = 0x00108093U, ecall = 0x00000073U, ebreak = 0x00100073U;
	TargetCoreType *c;
	memset(memory, 0, sizeof(memory));
	for (n = 0; n < 10; n++)put_insn(n * 4, addi);
	put_insn(40, ecall);
	put_insn(44, ebreak);
	virtual_cpu.core_id_num = 1;
	cpu_init();
	device_init(&virtual_cpu, &clock);
	c = &virtual_cpu.cores[0].core;
	for (n = 0; n < 10; n++) {
		device_supply_clock(&clock);
		cpu_supply_clock(0);
		clock.clock++;
	}
	if (clock.clock != 10 || c->internal_cycle != 10 || c->internal_instret != 10 ||
	    c->reg.x[1] != 10 ||
	    virtual_cpu.cores[0].elaps != 10)return 1;
	c->reg.csr.mtvec = 44;
	cpu_supply_clock(0);
	if (c->internal_cycle != 11 || c->internal_instret != 10 || c->reg.csr.mcause != 11 ||
	    c->reg.csr.mepc != 40)return 2;
	cpu_supply_clock(0);
	if (c->reg.csr.mcause != 3 || c->internal_instret != 10)return 3;

	/* Cache target 16, rewrite it through SW at 0, then execute the new form. */
	cpu_reset(0);
	c = &virtual_cpu.cores[0].core;
	put_insn(16, 0x00100293U);
	c->reg.pc = 16;
	cpu_supply_clock(0);
	put_insn(0, 0x0020a023U);
	put_insn(4, 0x00c0006fU);
	c->reg.x[1] = 16;
	c->reg.x[2] = 0x00200293U;
	c->reg.pc = 0;
	cpu_supply_clock(0);
	cpu_supply_clock(0);
	cpu_supply_clock(0);
	if (c->reg.x[5] != 2)return 4;

	/* Misaligned SD/LD are split and preserve the complete scalar value. */
	cpu_reset(0);
	c = &virtual_cpu.cores[0].core;
	put_insn(0, 0x0020b023U);
	put_insn(4, 0x0000b183U);
	c->reg.x[1] = 101;
	c->reg.x[2] = 0x8877665544332211ULL;
	cpu_supply_clock(0);
#if CPU_CONFIG_ERBIUM_COMPAT
	cpu_supply_clock(0);
	if (c->reg.x[3] != 0x8877665544332211ULL)return 5;
#else
	if (c->reg.csr.mcause != 6U)return 5;
#endif

	/* Erbium architectural counters read zero while internal counters advance. */
	cpu_reset(0);
	c = &virtual_cpu.cores[0].core;
	put_insn(0, 0xb0002273U);
	cpu_supply_clock(0);
	if (c->reg.x[4] != 0 || c->internal_cycle != 1 || c->internal_instret != 1)return 6;

	cpu_reset(0);
	c = &virtual_cpu.cores[0].core;
	put_insn(0, 0x0000100fU);
	c->reg.csr.mtvec = 32;
	cpu_supply_clock(0);
#if CPU_CONFIG_ERBIUM_COMPAT
	if (c->reg.csr.mcause != 30 || c->reg.csr.mtval != 0x0000100fU || c->reg.pc != 32)return 7;
#else
	if (c->reg.pc != 4 || c->internal_instret != 1)return 7;
#endif

	cpu_reset(0);
	c = &virtual_cpu.cores[0].core;
	put_insn(0, 0x10500073U);
	c->reg.csr.mie = 1ULL << 3;
	cpu_supply_clock(0);
	if (!c->is_halt)return 8;
	intc_cpu_trigger_interrupt(0, 3);
	if (c->is_halt)return 9;
	{
		uint32 value = 0;
		clock.clock = 100;
		device_supply_clock(&clock);
		erbium_timer_ops.get_data32(NULL, 0, 0x80f40200ULL, &value);
		if (value != 1U)return 10;
	}

	/* The public interrupt API raises a PLIC source and reaches trap entry. */
	cpu_reset(0);
	c = &virtual_cpu.cores[0].core;
	c->reg.csr.mtvec = 32;
	c->reg.csr.mstatus = 1ULL << 3;
	c->reg.csr.mie = 1ULL << 11;
	intc_plic_ops.put_data32(NULL, 0, 0xa0002000ULL, 1U << 4);
	if (intc_raise_intr(4U) != 0)return 11;
	cpu_supply_clock(0);
	if (c->reg.csr.mcause != ((1ULL << 63) | 11U) || c->reg.pc != 32U)return 12;
	return 0;
}
