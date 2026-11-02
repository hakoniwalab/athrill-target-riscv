/*
 * RISC-V local interrupt controller and Erbium PLIC.
 *
 * PLIC register map (base 0xa0000000):
 *   0x000004..0x000018  source priorities 1..6 (fixed to priority 1)
 *   0x001000             pending bits
 *   0x002000             hart 0 machine-context enables
 *   0x200000             hart 0 machine-context threshold
 *   0x200004             hart 0 machine-context claim/complete
 */
#include "intc.h"
#include "std_errno.h"
#include "mmio_register.h"

#include <string.h>

#define PLIC_BASE 0xa0000000ULL
#define PLIC_PENDING_OFFSET 0x1000U
#define PLIC_ENABLE_OFFSET 0x2000U
#define PLIC_THRESHOLD_OFFSET 0x200000U
#define PLIC_CLAIM_OFFSET 0x200004U
#define PLIC_SOURCE_MASK 0x7eU
#define MIP_MEIP 11U

uint8 intc_plic_data[0x200008];

static uint32 plic_pending;
static uint32 plic_asserted;
static uint32 plic_enable;
static uint32 plic_active;
static uint32 plic_threshold;

static void intc_plic_update(void)
{
	uint32 eligible = plic_pending & plic_enable & PLIC_SOURCE_MASK;

	intc_set_local_interrupt(0U, MIP_MEIP,
	                         (eligible != 0U) && (plic_threshold < 1U));
}

void intc_plic_set_source(uint32 source, std_bool level)
{
	uint32 bit;

	if ((source < INTC_PLIC_SOURCE_MIN) || (source > INTC_PLIC_SOURCE_MAX)) {
		return;
	}
	bit = 1U << source;
	if (level) {
		plic_asserted |= bit;
		if (plic_active != source) {
			plic_pending |= bit;
		}
	} else {
		plic_asserted &= ~bit;
	}
	intc_plic_update();
}

int intc_raise_intr(uint32 intno)
{
	/* External interrupt numbers are the PLIC source IDs 1 through 6. */
	if ((intno < INTC_PLIC_SOURCE_MIN) || (intno > INTC_PLIC_SOURCE_MAX)) {
		return -1;
	}
	intc_plic_set_source(intno, TRUE);
	intc_plic_set_source(intno, FALSE);
	return 0;
}

static uint32 intc_plic_claim(void)
{
	uint32 source;

	for (source = INTC_PLIC_SOURCE_MIN;
	     source <= INTC_PLIC_SOURCE_MAX; source++) {
		if ((plic_pending & plic_enable & (1U << source)) != 0U) {
			plic_pending &= ~(1U << source);
			plic_active = source;
			intc_plic_update();
			return source;
		}
	}
	return 0U;
}

static uint32 intc_plic_read_register(AthrillAddrType address)
{
	uint32 offset = (uint32)(address - PLIC_BASE);

	if (offset < 0x1cU) {
		return ((offset / 4U) <= INTC_PLIC_SOURCE_MAX) ? 1U : 0U;
	}
	if (offset == PLIC_PENDING_OFFSET) {
		return plic_pending;
	}
	if (offset == PLIC_ENABLE_OFFSET) {
		return plic_enable;
	}
	if (offset == PLIC_THRESHOLD_OFFSET) {
		return plic_threshold;
	}
	if (offset == PLIC_CLAIM_OFFSET) {
		return intc_plic_claim();
	}
	return 0U;
}

static void intc_plic_write_register(AthrillAddrType address, uint32 value)
{
	uint32 offset = (uint32)(address - PLIC_BASE);

	if (offset == PLIC_ENABLE_OFFSET) {
		plic_enable = value & PLIC_SOURCE_MASK;
	} else if (offset == PLIC_THRESHOLD_OFFSET) {
		plic_threshold = value;
	} else if ((offset == PLIC_CLAIM_OFFSET) && (value == plic_active)) {
		uint32 bit = 1U << plic_active;

		plic_active = 0U;
		if ((plic_asserted & bit) != 0U) {
			plic_pending |= bit;
		}
	}
	intc_plic_update();
}

static Std_ReturnType intc_plic_get8(MpuAddressRegionType *region,
                                     CoreIdType core_id, AthrillAddrType address, uint8 *data)
{
	(void)region;
	(void)core_id;
	*data = (uint8)mmio_read(intc_plic_read_register, address, 1U);
	return STD_E_OK;
}

static Std_ReturnType intc_plic_get16(MpuAddressRegionType *region,
                                      CoreIdType core_id, AthrillAddrType address, uint16 *data)
{
	(void)region;
	(void)core_id;
	*data = (uint16)mmio_read(intc_plic_read_register, address, 2U);
	return STD_E_OK;
}

static Std_ReturnType intc_plic_get32(MpuAddressRegionType *region,
                                      CoreIdType core_id, AthrillAddrType address, uint32 *data)
{
	(void)region;
	(void)core_id;
	*data = mmio_read(intc_plic_read_register, address, 4U);
	return STD_E_OK;
}

static Std_ReturnType intc_plic_put8(MpuAddressRegionType *region,
                                     CoreIdType core_id, AthrillAddrType address, uint8 data)
{
	(void)region;
	(void)core_id;
	mmio_write(intc_plic_write_register, address, 1U, data);
	return STD_E_OK;
}

static Std_ReturnType intc_plic_put16(MpuAddressRegionType *region,
                                      CoreIdType core_id, AthrillAddrType address, uint16 data)
{
	(void)region;
	(void)core_id;
	mmio_write(intc_plic_write_register, address, 2U, data);
	return STD_E_OK;
}

static Std_ReturnType intc_plic_put32(MpuAddressRegionType *region,
                                      CoreIdType core_id, AthrillAddrType address, uint32 data)
{
	(void)region;
	(void)core_id;
	mmio_write(intc_plic_write_register, address, 4U, data);
	return STD_E_OK;
}

MpuAddressRegionOperationType intc_plic_ops = {
	intc_plic_get8,
	intc_plic_get16,
	intc_plic_get32,
	intc_plic_put8,
	intc_plic_put16,
	intc_plic_put32,
	NULL
};

void intc_reset(void)
{
	plic_pending = 0U;
	plic_asserted = 0U;
	plic_enable = 0U;
	plic_active = 0U;
	plic_threshold = 0U;
	memset(intc_plic_data, 0, sizeof(intc_plic_data));
	intc_plic_update();
}
