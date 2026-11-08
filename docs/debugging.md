# Debugging firmware with GDB or LLDB

Athrill RISC-V runs a GDB remote server when it is started with `-g<port>`.
GDB (`gdb-multiarch` or a RISC-V GDB) and LLDB connect to it. You can then
set breakpoints, step, read the backtrace, and read and write registers and
memory of PX4, NuttX or any other firmware on the emulated Erbium.

The server is in the Athrill core. This target needs a core whose server
sends a target description and takes 64-bit addresses
(toppers/athrill#85, #86). With such a core the server is built in by
default on hosts with POSIX sockets; `-DATHRILL_ENABLE_GDB_RSP=OFF` leaves it
out.

Without `-g`, Athrill runs as before: the same fast execution path, the
same instruction counts and results.

## Start Athrill with the server

Add `-g<port>` to the usual command. Athrill listens on `127.0.0.1:<port>` and
stops before the first instruction until a debugger connects:

```bash
athrill3 -c1 -g1234 -m memory.txt -d device_config.txt -t -1 aifoundry_erbium_default.elf
```

## GDB

```text
$ gdb-multiarch aifoundry_erbium_default.elf
(gdb) target remote :1234
(gdb) break Sih::sensor_step
(gdb) continue
Breakpoint 1, Sih::sensor_step (this=0x40172f58) at .../sih.cpp:244
(gdb) bt
#0  Sih::sensor_step (this=0x40172f58) at .../sih.cpp:244
#1  Sih::realtime_loop (this=0x40172f58) at .../sih.cpp:211
#2  Sih::run (this=0x40172f58) at .../sih.cpp:89
(gdb) info registers pc sp ra mstatus mcause
(gdb) next
(gdb) x/2gx $sp
(gdb) detach
```

## LLDB

LLDB reads the same target description, so nothing else is needed:

```text
$ lldb aifoundry_erbium_default.elf
(lldb) gdb-remote 1234
(lldb) breakpoint set -n Sih::sensor_step
(lldb) continue
(lldb) bt
(lldb) register read pc sp ra mstatus mcause fcsr
(lldb) next
(lldb) detach
```

## What the debugger sees

- Registers, in GDB's RISC-V features:
  - x0-x31 by ABI name (`zero`, `ra`, `sp`, ...; x0 reads 0);
  - pc;
  - f0-f31, 32 bits each (Erbium has the F extension only), plus fflags, frm and fcsr;
  - mstatus, misa, mie, mtvec, mscratch, mepc, mcause, mtval, mip and mhartid. misa and mhartid are read only.
- Addresses are 64 bits: breakpoints and memory accesses work above 4 GiB.
- Breakpoints stop before the instruction at their address. A step runs one
  instruction (`stepi`); `next` and `step` are the debugger's line stepping
  on top of it.
- Debugging together with a Hakoniwa-connected run (`--hakoniwa`) is not
  supported yet (toppers/athrill#79).
