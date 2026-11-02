#include "concrete_executor/target/dbg_target_serial.h"
#include "peripheral/erbium_devices.h"
#include <stdio.h>
static std_bool serial_escape;
Std_ReturnType dbg_serial_in(uint8 c,uint8 d){if(c!=0)return STD_E_INVALID;if(serial_escape){serial_escape=FALSE;if(d=='n')d='\n';else if(d=='r')d='\r';else{Std_ReturnType e=erbium_uart_receive('\\');if(e!=STD_E_OK)return e;}}else if(d=='\\'){serial_escape=TRUE;return STD_E_OK;}return erbium_uart_receive(d);}
bool dbg_serial_getchar(uint8 c,uint8*d){(void)c;(void)d;return FALSE;}
bool dbg_serial_putchar(uint8 c,uint8 d){FILE*f=c?stderr:stdout;fputc(d,f);fflush(f);return TRUE;}
bool dbg_serial_getchar_file(uint8 c,uint8*d){return dbg_serial_getchar(c,d);}
bool dbg_serial_putchar_file(uint8 c,uint8 d){return dbg_serial_putchar(c,d);}
void dbg_serial_flush_file(uint8 c){(void)c;fflush(stdout);fflush(stderr);}
