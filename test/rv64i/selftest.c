#include <stdint.h>
#define SERIAL (*(volatile uint8_t *)0x10000000UL)
static void puts(const char*s){while(*s)SERIAL=(uint8_t)*s++;}
static void result(const char*n,int ok){puts(ok?"PASS ":"FAIL ");puts(n);puts("\n");}
static volatile uint64_t mem[4];
int main(void){volatile uint64_t a,b,v;int ok;
 puts("Hello RV64I\n");
 a=0x123456789abcdef0ULL;b=0x1111111111111111ULL;result("arithmetic",a+b==0x23456789abcdf001ULL&&a-b==0x0123456789abcddfULL&&(a^b)==0x032547698badcfe1ULL&&(a|b)==0x133557799bbddff1ULL&&(a&b)==0x1010101010101010ULL);
 v=4;a=0x8000000000000001ULL;result("shifts",(a<<v)==0x10ULL&&(a>>v)==0x0800000000000000ULL&&((int64_t)a>>v)==(int64_t)0xf800000000000000ULL);
 result("compares",((int64_t)-2<(int64_t)1)&&!((uint64_t)-2<1)&&((uint64_t)2<3));
 ok=0;for(a=0;a<7;a++){if(a==3)continue;ok+=(int)a;}result("branches",ok==18);
 mem[0]=0x80ff8001ffffffffULL;{volatile int8_t*s8=(volatile int8_t*)mem;volatile uint8_t*u8=(volatile uint8_t*)mem;volatile int16_t*s16=(volatile int16_t*)mem;volatile uint16_t*u16=(volatile uint16_t*)mem;volatile int32_t*s32=(volatile int32_t*)mem;volatile uint32_t*u32=(volatile uint32_t*)mem;result("loads",s8[0]==-1&&u8[0]==255&&s16[1]==-1&&u16[1]==65535&&s32[0]==-1&&u32[0]==0xffffffffU&&mem[0]==0x80ff8001ffffffffULL);}
 {volatile uint8_t*p=(volatile uint8_t*)&mem[1];*(volatile uint8_t*)(p+0)=0x12;*(volatile uint16_t*)(p+2)=0x3456;*(volatile uint32_t*)(p+4)=0x89abcdefU;mem[2]=0x0123456789abcdefULL;result("stores",p[0]==0x12&&*(volatile uint16_t*)(p+2)==0x3456&&*(volatile uint32_t*)(p+4)==0x89abcdefU&&mem[2]==0x0123456789abcdefULL);}
 {int32_t x=(int32_t)0x7fffffffU,y=1,z;asm volatile("addw %0,%1,%2":"=r"(z):"r"(x),"r"(y));ok=(z==(int32_t)0x80000000U);asm volatile("subw %0,%1,%2":"=r"(z):"r"(y),"r"(x));ok&=(z==(int32_t)0x80000002U);asm volatile("slliw %0,%1,1":"=r"(z):"r"(x));ok&=(z==-2);asm volatile("sraiw %0,%1,1":"=r"(z):"r"((int32_t)0x80000000U));ok&=(z==(int32_t)0xc0000000U);result("word",ok);}
 {uint64_t c0,c1,i0,i1;asm volatile("csrr %0,mcycle":"=r"(c0));asm volatile("csrr %0,minstret":"=r"(i0));asm volatile("nop;nop;nop;nop;nop;nop;nop;nop");asm volatile("csrr %0,mcycle":"=r"(c1));asm volatile("csrr %0,minstret":"=r"(i1));result("counters",c1>c0&&i1>i0&&(c1-c0)==(i1-i0));}
 puts("SELFTEST DONE\n");return 0;}
