#include "concrete_executor/target/dbg_target_serial.h"
#include "device_tree.h"
#include "std_errno.h"
#include <stdio.h>
#include <unistd.h>
static std_bool serial_escape;
/* A character typed in the debugger: to the board's console UART model
 * (console_listen, ABI 1.17). */
static Std_ReturnType serial_receive(uint8 d)
{
	return riscv_device_tree_console_input(d) ? STD_E_OK : STD_E_LIMIT;
}
Std_ReturnType dbg_serial_in(uint8 c,uint8 d){if(c!=0)return STD_E_INVALID;if(serial_escape){serial_escape=FALSE;if(d=='n')d='\n';else if(d=='r')d='\r';else{Std_ReturnType e=serial_receive('\\');if(e!=STD_E_OK)return e;}}else if(d=='\\'){serial_escape=TRUE;return STD_E_OK;}return serial_receive(d);}
bool dbg_serial_getchar(uint8 c,uint8*d){(void)c;(void)d;return FALSE;}
/*
 * Console output. On a terminal every byte is flushed, so the console can
 * be watched live. To a file or a pipe, flushing every byte costs a system
 * call per byte (a third of the run time with a guest that writes to its
 * UART all the time); there the output is flushed at each newline and by
 * stdio at exit (and dbg_serial_flush_file()).
 */
static int serial_terminal[2] = { -1, -1 };	/* stdout, stderr: -1 not known yet */

static std_bool serial_is_terminal(uint8 c, FILE *f)
{
	uint32 index = (c != 0U) ? 1U : 0U;

	if (serial_terminal[index] < 0) {
		serial_terminal[index] = isatty(fileno(f)) ? 1 : 0;
	}
	return serial_terminal[index] == 1;
}

bool dbg_serial_putchar(uint8 c, uint8 d)
{
	FILE *f = (c != 0U) ? stderr : stdout;

	fputc(d, f);
	if (serial_is_terminal(c, f) || (d == '\n')) {
		fflush(f);
	}
	return TRUE;
}
bool dbg_serial_getchar_file(uint8 c,uint8*d){return dbg_serial_getchar(c,d);}
bool dbg_serial_putchar_file(uint8 c,uint8 d){return dbg_serial_putchar(c,d);}
void dbg_serial_flush_file(uint8 c){(void)c;fflush(stdout);fflush(stderr);}
