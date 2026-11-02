#include "erbium_devices.h"
#include "cpu.h"
#include <string.h>
#define PLIC_BASE 0xa0000000ULL
uint8 erbium_plic_data[0x200008];
static uint32 pending,enable,threshold,active;
static void update(void){if((pending&enable&~1U)!=0)intc_cpu_trigger_interrupt(0,11);else virtual_cpu.cores[0].core.reg.csr.mip&=~(1ULL<<11);}
void erbium_plic_set_source(unsigned s,std_bool on){if(s<32){if(on)pending|=1U<<s;else pending&=~(1U<<s);update();}}
static uint32 claim(void){unsigned s;for(s=1;s<=6;s++)if((pending&enable&(1U<<s))!=0){pending&=~(1U<<s);active=s;update();return s;}return 0;}
static uint32 rd(AthrillAddrType a){uint32 o=(uint32)(a-PLIC_BASE);if(o<0x1c)return(o/4)<=6?1:0;if(o==0x1000)return pending;if(o==0x2000)return enable;if(o==0x200000)return threshold;if(o==0x200004)return claim();return 0;}
static void wr(AthrillAddrType a,uint32 v){uint32 o=(uint32)(a-PLIC_BASE);if(o==0x2000)enable=v&0x7eU;else if(o==0x200000)threshold=0;else if(o==0x200004&&v==active)active=0;update();}
static Std_ReturnType g8(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint8*d){uint32 v=rd(a&~3ULL);(void)r;(void)c;*d=(uint8)(v>>((a&3U)*8U));return STD_E_OK;}static Std_ReturnType g16(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint16*d){uint8 x,y;g8(r,c,a,&x);g8(r,c,a+1,&y);*d=x|((uint16)y<<8);return STD_E_OK;}static Std_ReturnType g32(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint32*d){(void)r;(void)c;*d=rd(a);return STD_E_OK;}static Std_ReturnType p8(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint8 d){(void)r;(void)c;wr(a,d);return STD_E_OK;}static Std_ReturnType p16(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint16 d){(void)r;(void)c;wr(a,d);return STD_E_OK;}static Std_ReturnType p32(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint32 d){(void)r;(void)c;wr(a,d);return STD_E_OK;}
MpuAddressRegionOperationType erbium_plic_ops={g8,g16,g32,p8,p16,p32,NULL};
void erbium_plic_reset(void){pending=enable=threshold=active=0;memset(erbium_plic_data,0,sizeof(erbium_plic_data));}
