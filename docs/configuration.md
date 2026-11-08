# RISC-V target configuration

This page covers parameters read by the Athrill RISC-V target for the AIFoundry Erbium board. Command-line options,
the memory configuration, the device-configuration grammar, and common parameters are in the
[Athrill core configuration reference](https://github.com/toppers/athrill/blob/master/docs/configuration.md).

The target has one RV64 core. Erbium firmware normally sets `DEVICE_CPU_FREQ 200` and names the board's Devicetree
with `DEVICE_CONFIG_DEVICE_TREE`. Paths are resolved from the process working directory.

## Common core parameter used by the target

| Key | RISC-V behavior | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CPU_FREQ` | Virtual CPU frequency in MHz. The board's models (the machine timer converting Athrill clocks to `mtime` ticks) read it as the CPU clock; the core uses it to convert clocks to simulated time. Use a positive decimal value. | The target reports `200` to the models when the key is absent, while the core falls back to `100`; set the key explicitly to keep them consistent. | `DEVICE_CPU_FREQ 200` |

The RISC-V target also implements the common core's idle-function and spin-halt hooks. It has no default idle
function symbol, but `DEVICE_CONFIG_IDLE_FUNCTION` can name one; `DEVICE_CONFIG_SPIN_HALT 1` recognizes `JAL x0, 0`
and compressed `C.J` to self when no interrupt is ready. Both behaviors and their clock-skip dependency are described
on the core page.

## Board (Devicetree)

The Erbium board's devices are [athrill-device](https://github.com/hakoniwalab/athrill-device) models built at
start-up from a Devicetree blob: the Shakti UART, the machine timer, the system registers and the MRAM controller
(`boards/erbium/erbium.dtb`, from Zephyr's `openhwgroup/erbium.dtsi` and Athrill's `erbium-athrill.dtsi`). The hart's
local interrupts and the PLIC (sources 1 to 6) stay in the target. The machine timer's frequency (2 MHz) is the
`clock-frequency` of its node; NuttX's `CONFIG_ERBIUM_MTIMER_FREQ` must match.

| Key | Meaning and accepted values | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CONFIG_DEVICE_TREE` | The board's Devicetree blob. Athrill logs its SHA-256 and every model it builds (`DEVICE_TREE`, `DEVICE_MODEL` lines); a blob that does not build stops Athrill. | Absent: no board devices (the CPU, its local interrupts and the PLIC only; enough for bare-metal tests). | `DEVICE_CONFIG_DEVICE_TREE athrill-device/boards/erbium/erbium.dtb` |
| `DEVICE_CONFIG_DEVICE_PLUGIN_<n>` | A model plugin for a compatible, as `compatible=library-path` (`,` written `%2C`); `<n>` counts from 0 without gaps. | Absent: built-in models only. | `DEVICE_CONFIG_DEVICE_PLUGIN_0 vendor%2Cchip=libchip.dylib` |

`DEVICE_CONFIG_ERBIUM_TIMER_FREQ` is no longer read: the frequency is part of the board description.

## Erbium UART

The Erbium UART (the athrill-device model `openhwgroup,shakti-uart-v1`) has 16-byte RX and TX FIFOs and advances
one character every 30 virtual clocks. Its two keys are host values the model reads. File input and TCP
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
