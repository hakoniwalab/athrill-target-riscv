#include "concrete_executor/target/dbg_target_cpu.h"
#include "cpu.h"
#include <inttypes.h>
#include <stdio.h>

void dbg_target_print_cpu(uint32 id)
{
    unsigned i; TargetCoreType *c=&virtual_cpu.cores[id].core;
    printf("***CPU<%u> RV64***\nPC 0x%016" PRIx64 " privilege %u\n",id,c->reg.pc,(unsigned)c->reg.privilege);
    for(i=0;i<32;i++) printf("x%-2u 0x%016" PRIx64 "%c",i,c->reg.x[i],(i%4)==3?'\n':' ');
    printf("mstatus 0x%016" PRIx64 " mtvec 0x%016" PRIx64 " mepc 0x%016" PRIx64 "\nmcause 0x%016" PRIx64 " mtval 0x%016" PRIx64 "\nmcycle 0x%016" PRIx64 " minstret 0x%016" PRIx64 "\n",c->reg.csr.mstatus,c->reg.csr.mtvec,c->reg.csr.mepc,c->reg.csr.mcause,c->reg.csr.mtval,c->reg.csr.mcycle,c->reg.csr.minstret);
}
void cpu_debug_print_mpu_status(uint32 id) { (void)id; }

#ifdef ATHRILL_ENABLE_GDB_RSP
#include "debugger/rsp/gdb_rsp_target.h"
#include <string.h>

/*
 * GDB RSP register image, little endian, in the order of the target
 * description below (GDB's and LLDB's RISC-V features):
 *    0-31  x0-x31 (ABI names; x0 reads 0, writes are ignored), 64 bits
 *      32  pc, 64 bits
 *   33-64  f0-f31, 32 bits (Erbium has F only: the low half of the
 *          NaN-boxed register; a write NaN-boxes)
 *   65-67  fflags, frm, fcsr, 32 bits
 *   68-77  mstatus, misa, mie, mtvec, mscratch, mepc, mcause, mtval, mip,
 *          mhartid, 64 bits (misa and mhartid read only)
 */
#define GDB_REG_PC 32U
#define GDB_REG_F0 33U
#define GDB_REG_FFLAGS 65U
#define GDB_REG_FRM 66U
#define GDB_REG_FCSR 67U
#define GDB_REG_CSR0 68U
#define GDB_REG_COUNT 78U

#define XREG(n) "<reg name=\"" n "\" bitsize=\"64\" type=\"int\"/>"
#define FREG(n) "<reg name=\"" n "\" bitsize=\"32\" type=\"ieee_single\" group=\"float\"/>"
#define CSR(n) "<reg name=\"" n "\" bitsize=\"64\" type=\"int\" group=\"system\"/>"

static const char target_xml[] =
	"<?xml version=\"1.0\"?>"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
	"<target version=\"1.0\">"
	"<architecture>riscv:rv64</architecture>"
	"<feature name=\"org.gnu.gdb.riscv.cpu\">"
	"<reg name=\"zero\" bitsize=\"64\" type=\"int\" regnum=\"0\"/>"
	"<reg name=\"ra\" bitsize=\"64\" type=\"code_ptr\"/>"
	"<reg name=\"sp\" bitsize=\"64\" type=\"data_ptr\"/>"
	XREG("gp") XREG("tp") XREG("t0") XREG("t1") XREG("t2")
	"<reg name=\"fp\" bitsize=\"64\" type=\"data_ptr\"/>"
	XREG("s1") XREG("a0") XREG("a1") XREG("a2") XREG("a3") XREG("a4") XREG("a5") XREG("a6") XREG("a7")
	XREG("s2") XREG("s3") XREG("s4") XREG("s5") XREG("s6") XREG("s7") XREG("s8") XREG("s9")
	XREG("s10") XREG("s11") XREG("t3") XREG("t4") XREG("t5") XREG("t6")
	"<reg name=\"pc\" bitsize=\"64\" type=\"code_ptr\" regnum=\"32\"/>"
	"</feature>"
	"<feature name=\"org.gnu.gdb.riscv.fpu\">"
	"<reg name=\"ft0\" bitsize=\"32\" type=\"ieee_single\" group=\"float\" regnum=\"33\"/>"
	FREG("ft1") FREG("ft2") FREG("ft3") FREG("ft4") FREG("ft5") FREG("ft6") FREG("ft7")
	FREG("fs0") FREG("fs1") FREG("fa0") FREG("fa1") FREG("fa2") FREG("fa3") FREG("fa4") FREG("fa5")
	FREG("fa6") FREG("fa7") FREG("fs2") FREG("fs3") FREG("fs4") FREG("fs5") FREG("fs6") FREG("fs7")
	FREG("fs8") FREG("fs9") FREG("fs10") FREG("fs11") FREG("ft8") FREG("ft9") FREG("ft10") FREG("ft11")
	"<reg name=\"fflags\" bitsize=\"32\" type=\"int\" group=\"float\" regnum=\"65\"/>"
	"<reg name=\"frm\" bitsize=\"32\" type=\"int\" group=\"float\"/>"
	"<reg name=\"fcsr\" bitsize=\"32\" type=\"int\" group=\"float\"/>"
	"</feature>"
	"<feature name=\"org.gnu.gdb.riscv.csr\">"
	"<reg name=\"mstatus\" bitsize=\"64\" type=\"int\" group=\"system\" regnum=\"68\"/>"
	CSR("misa") CSR("mie") CSR("mtvec") CSR("mscratch") CSR("mepc") CSR("mcause") CSR("mtval")
	CSR("mip") CSR("mhartid")
	"</feature>"
	"</target>";

const char *dbg_target_gdb_target_xml(void)
{
	return target_xml;
}

uint32 dbg_target_gdb_reg_count(void)
{
	return GDB_REG_COUNT;
}

uint32 dbg_target_gdb_reg_size(uint32 regno)
{
	return (regno >= GDB_REG_F0 && regno < GDB_REG_CSR0) ? 4U : 8U;
}

static uint64 *csr_of(TargetCoreType *c, uint32 regno)
{
	RiscvCsrType *csr = &c->reg.csr;
	uint64 *const table[] = {
		&csr->mstatus, &csr->misa, &csr->mie, &csr->mtvec, &csr->mscratch,
		&csr->mepc, &csr->mcause, &csr->mtval, &csr->mip, &csr->mhartid
	};
	return (regno >= GDB_REG_CSR0 && regno < GDB_REG_COUNT) ? table[regno - GDB_REG_CSR0] : NULL;
}

static void put_le(uint8 *bytes, uint64 value, uint32 size)
{
	uint32 i;
	for (i = 0U; i < size; i++) {
		bytes[i] = (uint8)(value >> (8U * i));
	}
}

static uint64 get_le(const uint8 *bytes, uint32 size)
{
	uint64 value = 0U;
	uint32 i;
	for (i = 0U; i < size; i++) {
		value |= (uint64)bytes[i] << (8U * i);
	}
	return value;
}

std_bool dbg_target_gdb_read_reg(uint32 core_id, uint32 regno, uint8 *bytes)
{
	TargetCoreType *c = &virtual_cpu.cores[core_id].core;
	uint64 *csr;

	if (regno < GDB_REG_PC) {
		put_le(bytes, (regno == 0U) ? 0U : c->reg.x[regno], 8U);
	}
	else if (regno == GDB_REG_PC) {
		put_le(bytes, c->reg.pc, 8U);
	}
	else if (regno < GDB_REG_FFLAGS) {
		put_le(bytes, c->reg.f[regno - GDB_REG_F0] & 0xffffffffULL, 4U);
	}
	else if (regno == GDB_REG_FFLAGS) {
		put_le(bytes, c->reg.fcsr & 0x1fU, 4U);
	}
	else if (regno == GDB_REG_FRM) {
		put_le(bytes, (c->reg.fcsr >> 5) & 0x7U, 4U);
	}
	else if (regno == GDB_REG_FCSR) {
		put_le(bytes, c->reg.fcsr, 4U);
	}
	else if ((csr = csr_of(c, regno)) != NULL) {
		put_le(bytes, *csr, 8U);
	}
	else {
		return FALSE;
	}
	return TRUE;
}

std_bool dbg_target_gdb_write_reg(uint32 core_id, uint32 regno, const uint8 *bytes)
{
	TargetCoreType *c = &virtual_cpu.cores[core_id].core;
	uint64 *csr;

	if (regno < GDB_REG_PC) {
		if (regno != 0U) {
			c->reg.x[regno] = get_le(bytes, 8U);
		}
	}
	else if (regno == GDB_REG_PC) {
		c->reg.pc = get_le(bytes, 8U);
	}
	else if (regno < GDB_REG_FFLAGS) {
		c->reg.f[regno - GDB_REG_F0] = 0xffffffff00000000ULL | get_le(bytes, 4U);
	}
	else if (regno == GDB_REG_FFLAGS) {
		c->reg.fcsr = (c->reg.fcsr & ~0x1fU) | ((uint32)get_le(bytes, 4U) & 0x1fU);
	}
	else if (regno == GDB_REG_FRM) {
		c->reg.fcsr = (c->reg.fcsr & ~0xe0U) | (((uint32)get_le(bytes, 4U) & 0x7U) << 5);
	}
	else if (regno == GDB_REG_FCSR) {
		c->reg.fcsr = (uint32)get_le(bytes, 4U) & 0xffU;
	}
	else if ((csr = csr_of(c, regno)) != NULL) {
		if (csr == &c->reg.csr.misa || csr == &c->reg.csr.mhartid) {
			return FALSE;
		}
		*csr = get_le(bytes, 8U);
	}
	else {
		return FALSE;
	}
	return TRUE;
}

void dbg_target_gdb_set_pc(uint32 core_id, AthrillAddrType pc)
{
	virtual_cpu.cores[core_id].core.reg.pc = pc;
}
#endif /* ATHRILL_ENABLE_GDB_RSP */
