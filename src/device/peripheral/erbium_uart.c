#include "erbium_devices.h"
#include <stdio.h>
#include <string.h>
#define UART_BASE 0x02004000ULL
uint8 erbium_uart_data[0x100];
static uint32 baud, control, ie, rx_threshold;
static uint32 read_reg(AthrillAddrType a){switch((unsigned)(a-UART_BASE)){case 0:return baud;case 0x18:return 1U;case 0x28:return control;case 0x30:return ie;case 0x40:return rx_threshold;default:return 0;}}
static void write_reg(AthrillAddrType a,uint32 v){switch((unsigned)(a-UART_BASE)){case 0:baud=v;break;case 8:putchar((uint8)v);fflush(stdout);break;case 0x28:control=v;break;case 0x30:ie=v;break;case 0x40:rx_threshold=v;break;}}
static Std_ReturnType g8(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint8*d){uint32 v=read_reg(a&~3ULL);(void)r;(void)c;*d=(uint8)(v>>((a&3U)*8U));return STD_E_OK;}
static Std_ReturnType g16(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint16*d){uint8 x,y;g8(r,c,a,&x);g8(r,c,a+1,&y);*d=(uint16)x|((uint16)y<<8);return STD_E_OK;}
static Std_ReturnType g32(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint32*d){(void)r;(void)c;*d=read_reg(a);return STD_E_OK;}
static Std_ReturnType p8(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint8 d){(void)r;(void)c;write_reg(a,d);return STD_E_OK;}
static Std_ReturnType p16(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint16 d){(void)r;(void)c;write_reg(a,d);return STD_E_OK;}
static Std_ReturnType p32(MpuAddressRegionType*r,CoreIdType c,AthrillAddrType a,uint32 d){(void)r;(void)c;write_reg(a,d);return STD_E_OK;}
MpuAddressRegionOperationType erbium_uart_ops={g8,g16,g32,p8,p16,p32,NULL};
void erbium_uart_reset(void){memset(erbium_uart_data,0,sizeof(erbium_uart_data));baud=control=ie=rx_threshold=0;}
