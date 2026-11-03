/*
 * Decode-cache benchmark: runs a four-instruction loop through
 * cpu_supply_clock() and prints the rate. With any argument the cache slot
 * is invalidated before every instruction, so each one is decoded again.
 *
 * Loop (hand-assembled):
 *   0x00000293 addi x5, x0, 0
 *   0x00128293 addi x5, x5, 1
 *   0x0552c313 xori x6, x5, 0x55
 *   0xff9ff06f jal  x0, -8
 */
#include "cpu.h"
#include "std_cpu_ops.h"
#include "std_device_ops.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define HOST_MEMORY_SIZE 64U
#include "common/host_memory_stub.h"

#define BENCHMARK_INSTRUCTIONS 30000000ULL
#define WARM_UP_INSTRUCTIONS 10000U

void device_init(CpuType *cpu, DeviceClockType *clock)
{
	(void)cpu;
	(void)clock;
}

void device_reset(CpuType *cpu, DeviceClockType *clock)
{
	(void)cpu;
	(void)clock;
}

static double now(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return (double)time.tv_sec + (double)time.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	const uint32 code[] = { 0x00000293U, 0x00128293U, 0x0552c313U, 0xff9ff06fU };
	TargetCoreType *core;
	std_bool decode_every_time = (argc > 1);
	uint64 n;
	double start;
	double end;

	(void)argv;
	memcpy(memory, code, sizeof(code));
	virtual_cpu.core_id_num = 1;
	cpu_init();
	core = &virtual_cpu.cores[0].core;
	for (n = 0; n < WARM_UP_INSTRUCTIONS; n++) {
		cpu_supply_clock(0);
	}
	start = now();
	for (n = 0; n < BENCHMARK_INSTRUCTIONS; n++) {
		if (decode_every_time) {
			core->decode_cache[(core->reg.pc >> 1) & (RISCV_DECODE_CACHE_ENTRIES - 1U)].valid = 0;
		}
		cpu_supply_clock(0);
	}
	end = now();
	printf("%s %.3f MIPS\n", decode_every_time ? "decode-every-time" : "cached",
		(double)BENCHMARK_INSTRUCTIONS / (end - start) / 1e6);
	return 0;
}
