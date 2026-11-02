#include "pmp.h"
#include <string.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static void cfg(TargetCoreType *c, unsigned i, uint8 v) { c->reg.csr.pmpcfg[i/8] |= (uint64)v << ((i%8)*8); }
int main(void)
{
    TargetCoreType c; memset(&c,0,sizeof(c)); c.reg.privilege=RISCV_PRIV_U;
    cfg(&c,0,0x0b); c.reg.csr.pmpaddr[0]=0x1000>>2; /* TOR RW: [0,1000) */
    CHECK(riscv_pmp_check(&c,0x100,4,CpuMemoryAccess_READ));
    CHECK(!riscv_pmp_check(&c,0x100,4,CpuMemoryAccess_EXEC));
    CHECK(!riscv_pmp_check(&c,0xfff,2,CpuMemoryAccess_READ));
    memset(&c,0,sizeof(c)); c.reg.privilege=RISCV_PRIV_U;
    cfg(&c,0,0x13); c.reg.csr.pmpaddr[0]=0x2000>>2; /* NA4 RW */
    CHECK(riscv_pmp_check(&c,0x2000,4,CpuMemoryAccess_WRITE));
    CHECK(!riscv_pmp_check(&c,0x2004,1,CpuMemoryAccess_READ));
    memset(&c,0,sizeof(c)); c.reg.privilege=RISCV_PRIV_U;
    cfg(&c,0,0x1f); c.reg.csr.pmpaddr[0]=(0x4000>>2)|1; /* 16-byte NAPOT RWX */
    CHECK(riscv_pmp_check(&c,0x4000,16,CpuMemoryAccess_EXEC));
    CHECK(!riscv_pmp_check(&c,0x4010,1,CpuMemoryAccess_READ));
    c.reg.privilege=RISCV_PRIV_M; CHECK(riscv_pmp_check(&c,0x4010,1,CpuMemoryAccess_READ));
    c.reg.csr.pmpcfg[0]=(uint64)0x9c; CHECK(!riscv_pmp_check(&c,0x4000,1,CpuMemoryAccess_READ));
    return 0;
}
