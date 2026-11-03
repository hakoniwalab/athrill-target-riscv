/*
 * Device register accesses touch each covered 32-bit register once.
 *
 * Expected values are written out by hand: the register at word address
 * 0x100 reads 0x44332211 and the one at 0x104 reads 0x88776655.
 */
#include "mmio_register.h"
#include <stdio.h>

std_bool riscv_device_accessed;
static unsigned failures;
static unsigned reads[2];
static unsigned writes[2];
static uint32 written[2];

static uint32 read_register(AthrillAddrType word_address)
{
	unsigned index = (unsigned)((word_address - 0x100U) / 4U);

	reads[index]++;
	return index == 0U ? 0x44332211U : 0x88776655U;
}

static void write_register(AthrillAddrType word_address, uint32 value)
{
	unsigned index = (unsigned)((word_address - 0x100U) / 4U);

	writes[index]++;
	written[index] = value;
}

static void clear(void)
{
	reads[0] = 0U;
	reads[1] = 0U;
	writes[0] = 0U;
	writes[1] = 0U;
	written[0] = 0U;
	written[1] = 0U;
}

static void check(const char *name, int passed)
{
	printf("%s %s\n", passed ? "PASS" : "FAIL", name);
	if (!passed) {
		failures++;
	}
}

int main(void)
{
	uint32 value;

	clear();
	value = mmio_read(read_register, 0x100U, 2U);
	check("halfword read: value", value == 0x2211U);
	check("halfword read: register read once", reads[0] == 1U && reads[1] == 0U);

	clear();
	value = mmio_read(read_register, 0x102U, 2U);
	check("upper halfword read", value == 0x4433U && reads[0] == 1U);

	clear();
	value = mmio_read(read_register, 0x103U, 1U);
	check("byte read", value == 0x44U && reads[0] == 1U);

	clear();
	value = mmio_read(read_register, 0x103U, 2U);
	check("halfword across a word: value", value == 0x5544U);
	check("halfword across a word: each register once", reads[0] == 1U && reads[1] == 1U);

	clear();
	value = mmio_read(read_register, 0x102U, 4U);
	check("word across a word", value == 0x66554433U && reads[0] == 1U && reads[1] == 1U);

	clear();
	mmio_write(write_register, 0x101U, 1U, 0xabU);
	check("byte write: lane 1, one write", written[0] == 0x0000ab00U && writes[0] == 1U);

	clear();
	mmio_write(write_register, 0x102U, 2U, 0xbeefU);
	check("halfword write: upper lanes", written[0] == 0xbeef0000U && writes[0] == 1U);

	clear();
	mmio_write(write_register, 0x100U, 4U, 0x12345678U);
	check("word write", written[0] == 0x12345678U && writes[0] == 1U && writes[1] == 0U);

	clear();
	mmio_write(write_register, 0x103U, 2U, 0xcafeU);
	check("halfword write across a word", written[0] == 0xfe000000U && written[1] == 0x000000caU
		&& writes[0] == 1U && writes[1] == 1U);
	check("writes never read", reads[0] == 0U && reads[1] == 0U);
	check("accesses mark the device as accessed", riscv_device_accessed == TRUE);

	printf("%s\n", failures == 0U ? "SELFTEST PASS" : "SELFTEST FAIL");
	return failures == 0U ? 0 : 1;
}
