# RISC-V target configuration

This page covers parameters read by the Athrill RISC-V target for the AIFoundry Erbium board. Command-line options,
the memory configuration, the device-configuration grammar, and common parameters are in the
[Athrill core configuration reference](https://github.com/toppers/athrill/blob/master/docs/configuration.md).

The target has one RV64 core. Erbium firmware normally sets `DEVICE_CPU_FREQ 200` and
`DEVICE_CONFIG_ERBIUM_TIMER_FREQ 2000000`. Paths are resolved from the process working directory.

## Common core parameter used by the target

| Key | RISC-V behavior | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CPU_FREQ` | Virtual CPU frequency in MHz. The Erbium timer uses it to convert Athrill clocks to `mtime` ticks; the core uses it to convert clocks to simulated time. Use a positive decimal value. | The Erbium timer model falls back to `200`, while the core falls back to `100`; set the key explicitly to keep them consistent. Zero is replaced by `200` in the timer model. | `DEVICE_CPU_FREQ 200` |

The RISC-V target also implements the common core's idle-function and spin-halt hooks. It has no default idle
function symbol, but `DEVICE_CONFIG_IDLE_FUNCTION` can name one; `DEVICE_CONFIG_SPIN_HALT 1` recognizes `JAL x0, 0`
and compressed `C.J` to self when no interrupt is ready. Both behaviors and their clock-skip dependency are described
on the core page.

## Erbium timer

| Key | Meaning and accepted values | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CONFIG_ERBIUM_TIMER_FREQ` | Decimal `mtime` frequency in Hz. The target advances `mtime` fractionally from the configured CPU MHz and schedules the next `mtimecmp` event. NuttX's `CONFIG_ERBIUM_MTIMER_FREQ` must match. | `2000000`; zero also selects `2000000`. | `DEVICE_CONFIG_ERBIUM_TIMER_FREQ 2000000` |

## Erbium UART

The Erbium UART has 16-byte RX and TX FIFOs and advances one character every 30 virtual clocks. File input and TCP
input may both feed the RX FIFO. When TCP is enabled, guest transmit bytes go to the TCP client instead of the
Athrill terminal.

| Key | Meaning and accepted values | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CONFIG_ERBIUM_UART_RX_FILE` | Binary input file. It is read from the beginning, one byte per character period while RX has room. Appended data is checked after EOF at roughly one-million-clock intervals. | Absent: no file input. Failure to open warns and leaves file input disabled. | `DEVICE_CONFIG_ERBIUM_UART_RX_FILE nsh-input.txt` |
| `DEVICE_CONFIG_ERBIUM_UART_TCP_PORT` | Decimal TCP port; use 1 through 65535. Athrill listens on `127.0.0.1` for one client at a time. The listener survives guest resets, and incoming bytes enter the UART at its character rate. | Absent: no TCP listener. A socket, bind, listen, or thread failure leaves it disabled. | `DEVICE_CONFIG_ERBIUM_UART_TCP_PORT 5760` |

## Instruction trace

| Key | Meaning and accepted values | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEBUG_FUNC_RISCV_TRACE` | Nonzero prints every executed instruction with hart, sequence, virtual clock, privilege, PC, encoding, register changes, and traced stores. It also selects the per-instruction path instead of cached block execution so every record is emitted. | `0`. Trace volume is large and reduces performance. | `DEBUG_FUNC_RISCV_TRACE 1` |
