#include "erbium_devices.h"
#include "cpu.h"
#include <string.h>
#define TIMER_BASE 0x80f40200ULL
#define ERBIUM_TIMER_DEFAULT_FREQUENCY 2000000U
#define ATHRILL_CPU_DEFAULT_FREQUENCY 100U
uint8 erbium_timer_data[0x10];
static uint64 mtime,mtimecmp,last_clock,remainder;
static uint32 cpu_frequency=ATHRILL_CPU_DEFAULT_FREQUENCY;
static uint32 timer_frequency=ERBIUM_TIMER_DEFAULT_FREQUENCY;
static uint64 cpu_hz(void){return(uint64)cpu_frequency*1000000ULL;}
void erbium_timer_configure(uint32 cpu_mhz,uint32 timer_hz){cpu_frequency=cpu_mhz?cpu_mhz:ATHRILL_CPU_DEFAULT_FREQUENCY;timer_frequency=(timer_hz?timer_hz:ERBIUM_TIMER_DEFAULT_FREQUENCY)/2U;remainder=0;}
static uint32 word(AthrillAddrType a){uint64 v=(a-TIMER_BASE)<8?mtime:mtimecmp;return(uint32)(v>>(((a-TIMER_BASE)&4U)*8U));}
static Std_ReturnType g8(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint8*d){uint32 v=word(a&~3ULL);(void)r;(void)c;*d=(uint8)(v>>((a&3U)*8U));return STD_E_OK;}
static Std_ReturnType g16(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint16*d){uint8 x,y;g8(r,c,a,&x);g8(r,c,a+1,&y);*d=x|((uint16)y<<8);return STD_E_OK;}
static Std_ReturnType g32(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint32*d){(void)r;(void)c;*d=word(a);return STD_E_OK;}
static void putword(AthrillAddrType a,uint32 d){uint64*m=(a-TIMER_BASE)<8?&mtime:&mtimecmp;unsigned s=((a-TIMER_BASE)&4U)*8U;*m=(*m&~(0xffffffffULL<<s))|((uint64)d<<s);}
static Std_ReturnType p8(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint8 d){uint32 v=word(a&~3ULL),s=(a&3U)*8U;(void)r;(void)c;putword(a&~3ULL,(v&~(255U<<s))|((uint32)d<<s));return STD_E_OK;}
static Std_ReturnType p16(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint16 d){p8(r,c,a,(uint8)d);p8(r,c,a+1,(uint8)(d>>8));return STD_E_OK;}
static Std_ReturnType p32(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint32 d){(void)r;(void)c;putword(a,d);return STD_E_OK;}
MpuAddressRegionOperationType erbium_timer_ops={g8,g16,g32,p8,p16,p32,NULL};
void erbium_timer_reset(void){mtime=0;mtimecmp=~0ULL;last_clock=0;remainder=0;memset(erbium_timer_data,0,sizeof(erbium_timer_data));}
void erbium_timer_tick(DeviceClockType*c){uint64 clocks=c->clock-last_clock,hz=cpu_hz(),scaled,until,ticks;last_clock=c->clock;if(clocks!=0U){scaled=clocks*(uint64)timer_frequency+remainder;mtime+=scaled/hz;remainder=scaled%hz;}if(mtime>=mtimecmp)intc_cpu_trigger_interrupt(0,7);else virtual_cpu.cores[0].core.reg.csr.mip&=~(1ULL<<7);if(mtimecmp>mtime){ticks=mtimecmp-mtime;if(ticks<=(~0ULL-remainder)/hz){until=ticks*hz-remainder;until=(until+timer_frequency-1U)/timer_frequency;if(until<c->min_intr_interval)c->min_intr_interval=until?until:1U;}}}
