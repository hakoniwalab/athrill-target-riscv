cmake_minimum_required(VERSION 3.16)
project(athrill3 VERSION 0.1.0 LANGUAGES C CXX)

# The Athrill sources are C11 with GNU extensions. GCC 15 defaults to C23, where
# bool is a keyword and std_types.h's typedef no longer compiles.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
include(GNUInstallDirs)
include(CTest)
find_package(Threads REQUIRED)

set(ATHRILL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../athrill" CACHE PATH "Athrill core checkout")
get_filename_component(ATHRILL_SOURCE_DIR "${ATHRILL_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ATHRILL_ADDRESS_WIDTH 64 CACHE STRING "Athrill bus address width" FORCE)
include("${ATHRILL_SOURCE_DIR}/cmake/AthrillCore.cmake")
set(ATHRILL_TARGET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
option(ATHRILL_ENABLE_EXDEV "Enable external device loading" ON)
option(RISCV_ERBIUM_COMPAT "Model AIFoundry Erbium behaviour (OFF: standard RV64)" ON)
# Batched execution in the core's background loop: up to this many clocks
# without a device tick while no device event is due (see cpuemu.cpp).
add_compile_definitions(ATHRILL_TARGET_BATCH_CLOCKS=1024U)
if(RISCV_ERBIUM_COMPAT)
  add_compile_definitions(CPU_CONFIG_ERBIUM_COMPAT=1)
else()
  add_compile_definitions(CPU_CONFIG_ERBIUM_COMPAT=0)
endif()

add_library(athrill_riscv_config INTERFACE)
target_compile_definitions(athrill_riscv_config INTERFACE
    CPUEMU_CLOCK_BUG_FIX SEARCH_REGION_OPTIMIZE DISABLE_BUS_ACCESS_LOG
    OPTIMIZE_USE_ONLY_1CPU MINIMUM_DEVICE_CONFIG DISABLE_DEVICE_VDEV DISABLE_CAN)
if(ATHRILL_ENABLE_EXDEV)
  target_compile_definitions(athrill_riscv_config INTERFACE EXDEV_ENABLE)
endif()
target_link_libraries(athrill_riscv_config INTERFACE Threads::Threads m ${CMAKE_DL_LIBS})

set(RISCV_INCLUDES
    "${ATHRILL_SRC_DIR}" "${ATHRILL_SRC_DIR}/lib/dwarf" "${ATHRILL_SRC_DIR}/bus"
    "${ATHRILL_SRC_DIR}/cpu" "${ATHRILL_SRC_DIR}/device/mpu"
    "${ATHRILL_SRC_DIR}/debugger/interaction" "${ATHRILL_SRC_DIR}/debugger/interaction/inc"
    "${ATHRILL_SRC_DIR}/debugger/executor" "${ATHRILL_SRC_DIR}/main"
    "${ATHRILL_TARGET_SOURCE_DIR}/cpu" "${ATHRILL_TARGET_SOURCE_DIR}/cpu/config"
    "${ATHRILL_TARGET_SOURCE_DIR}/cpu/cpu_dec" "${ATHRILL_TARGET_SOURCE_DIR}/cpu/cpu_exec"
    "${ATHRILL_TARGET_SOURCE_DIR}/cpu/mpu"
    "${ATHRILL_TARGET_SOURCE_DIR}/device" "${ATHRILL_TARGET_SOURCE_DIR}/device/intc"
    "${ATHRILL_TARGET_SOURCE_DIR}/device/peripheral"
    "${ATHRILL_TARGET_SOURCE_DIR}/debugger/interaction/inc")
function(riscv_target name)
  athrill_apply_common_settings(${name})
  target_include_directories(${name} PRIVATE ${RISCV_INCLUDES})
  target_link_libraries(${name} PRIVATE athrill_riscv_config)
endfunction()

add_library(athrill_main STATIC ${ATHRILL_MAIN_SOURCES})
riscv_target(athrill_main)
target_link_libraries(athrill_main PUBLIC ${ATHRILL_HAKONIWA_LIBRARIES})

add_library(athrill_cui STATIC ${ATHRILL_CUI_SOURCES})
target_include_directories(athrill_cui PRIVATE "${ATHRILL_SRC_DIR}/debugger/executor/concrete_executor/target")
riscv_target(athrill_cui)

set(RISCV_CPU_SOURCES
  src/cpu/config/cpu_config.c
  src/cpu/idle_spin_halt_riscv.c
  src/device/intc/intc_cpu.c
  src/cpu/cpu_dec/riscv_decode.c
  src/cpu/cpu_dec/riscv_instruction_table.c
  src/cpu/cpu_dec/riscv_compressed_table.c
  src/cpu/cpu_exec/op_exec.c
  src/cpu/cpu_exec/op_exec_arithm.c
  src/cpu/cpu_exec/op_exec_logic.c
  src/cpu/cpu_exec/op_exec_shift.c
  src/cpu/cpu_exec/op_exec_muldiv.c
  src/cpu/cpu_exec/op_exec_load.c
  src/cpu/cpu_exec/op_exec_store.c
  src/cpu/cpu_exec/op_exec_branch.c
  src/cpu/cpu_exec/op_exec_system.c
  src/cpu/cpu_exec/op_exec_fpu.c
  src/cpu/cpu_exec/op_exec_fpu_arithmetic.c
  src/cpu/cpu_exec/op_exec_fpu_compare.c
  src/cpu/cpu_exec/op_exec_fpu_convert.c
  src/cpu/mpu/pmp.c)

add_library(athrill_riscv_cpu STATIC
  ${RISCV_CPU_SOURCES}
  "${ATHRILL_SRC_DIR}/lib/dbg_log.c")
riscv_target(athrill_riscv_cpu)

set(DEVICE_SOURCES ${ATHRILL_DEVICE_COMMON_SOURCES} ${ATHRILL_DEVICE_THREAD_SOURCES}
  src/device/device.c src/device/peripheral/erbium_sysreg.c src/device/peripheral/erbium_uart.c src/device/peripheral/erbium_uart_tcp.c src/device/peripheral/erbium_timer.c
  src/device/intc/intc.c src/debugger/executor/concrete_executor/dbg_target_cpu.c
  src/debugger/executor/concrete_executor/dbg_target_serial.c)
if(ATHRILL_ENABLE_EXDEV)
  list(APPEND DEVICE_SOURCES ${ATHRILL_DEVICE_EXDEV_SOURCES} ${ATHRILL_DEVICE_EXDEV_POSIX_SOURCES})
endif()
add_library(athrill_riscv_device STATIC ${DEVICE_SOURCES})
target_include_directories(athrill_riscv_device PRIVATE
  "${ATHRILL_SRC_DIR}/device/peripheral/target" "${ATHRILL_SRC_DIR}/device/peripheral/serial/fifo"
  "${ATHRILL_APL_DIR}/include")
riscv_target(athrill_riscv_device)

add_library(athrill_bus STATIC ${ATHRILL_BUS_SOURCES})
riscv_target(athrill_bus)
add_library(athrill_loader STATIC ${ATHRILL_LOADER_SOURCES})
riscv_target(athrill_loader)
add_library(athrill_mpu STATIC ${ATHRILL_MPU_SOURCES} src/cpu/config/mpu_config.c)
target_include_directories(athrill_mpu PRIVATE "${ATHRILL_SRC_DIR}/device/peripheral/serial/fifo" "${ATHRILL_SRC_DIR}/lib/dwarf/data_type")
riscv_target(athrill_mpu)
set(STD_SOURCES ${ATHRILL_STD_COMMON_SOURCES})
if(NOT WIN32)
  list(APPEND STD_SOURCES ${ATHRILL_STD_POSIX_SOURCES})
endif()
add_library(athrill_std STATIC ${STD_SOURCES})
riscv_target(athrill_std)

add_executable(athrill3 "${ATHRILL_MAIN_ENTRY_SOURCE}")
riscv_target(athrill3)
target_link_libraries(athrill3 PRIVATE athrill_main athrill_cui athrill_riscv_cpu
  athrill_riscv_device athrill_bus athrill_loader athrill_mpu athrill_std athrill_riscv_config)
if(ATHRILL_HAKONIWA_RPATH)
 set_target_properties(athrill3 PROPERTIES BUILD_RPATH "${ATHRILL_HAKONIWA_RPATH}" INSTALL_RPATH "${ATHRILL_HAKONIWA_RPATH}")
endif()
install(TARGETS athrill3 RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
  add_test(NAME athrill3-usage COMMAND athrill3)
  add_executable(athrill-pmp-test test/pmp_test.c src/cpu/mpu/pmp.c)
  target_include_directories(athrill-pmp-test PRIVATE src/cpu src/cpu/config src/cpu/mpu)
  riscv_target(athrill-pmp-test)
  add_test(NAME athrill-pmp COMMAND athrill-pmp-test)
  add_executable(athrill-clock-counter-test test/clock_counter_test.c
    ${RISCV_CPU_SOURCES}
    "${ATHRILL_SRC_DIR}/main/athrill_idle_spin_halt.c"
    src/device/device.c src/device/peripheral/erbium_sysreg.c src/device/peripheral/erbium_uart.c src/device/peripheral/erbium_uart_tcp.c src/device/peripheral/erbium_timer.c
    src/device/intc/intc.c)
  riscv_target(athrill-clock-counter-test)
  add_test(NAME athrill-clock-counter COMMAND athrill-clock-counter-test)
  add_executable(athrill-symbol-lookup-test "${ATHRILL_SOURCE_DIR}/test/symbol_lookup_test.c"
    "${ATHRILL_SRC_DIR}/lib/symbol_ops.c")
  riscv_target(athrill-symbol-lookup-test)
  add_test(NAME athrill-symbol-lookup COMMAND athrill-symbol-lookup-test)
  add_executable(athrill-mmio-register-test test/mmio_register_test.c)
  riscv_target(athrill-mmio-register-test)
  add_test(NAME athrill-mmio-register COMMAND athrill-mmio-register-test)
  add_executable(athrill-uart-tx-test test/uart_tx_test.c src/device/peripheral/erbium_uart.c src/device/peripheral/erbium_uart_tcp.c)
  riscv_target(athrill-uart-tx-test)
  add_test(NAME athrill-uart-tx COMMAND athrill-uart-tx-test)
  # Decoder dump for test/crosscheck_decoder.py (needs Docker; not a CTest).
  add_executable(athrill-decoder-dump test/decoder_dump.c test/common/host_cpuemu_stub.c)
  riscv_target(athrill-decoder-dump)
  target_link_libraries(athrill-decoder-dump PRIVATE athrill_riscv_cpu
    athrill_riscv_device athrill_bus athrill_mpu athrill_std
    athrill_riscv_config athrill_riscv_device)
  add_executable(athrill-decode-benchmark test/decode_benchmark.c
    ${RISCV_CPU_SOURCES} test/common/idle_spin_halt_stub.c)
  riscv_target(athrill-decode-benchmark)
  add_executable(athrill-decode-test test/decode_test.c test/common/host_cpuemu_stub.c)
  riscv_target(athrill-decode-test)
  # The memory map in athrill_riscv_config refers to the device registers,
  # so the device library follows it again for single-pass linkers (GNU ld).
  target_link_libraries(athrill-decode-test PRIVATE athrill_riscv_cpu
    athrill_riscv_device athrill_bus athrill_mpu athrill_std
    athrill_riscv_config athrill_riscv_device)
  add_test(NAME athrill-decode COMMAND athrill-decode-test)

  # Firmware tests: build the programs with test/build-firmware.sh into
  # RISCV_FIRMWARE_DIR (Docker cross toolchain), then re-run CMake.
  set(RISCV_FIRMWARE_DIR "${CMAKE_BINARY_DIR}/firmware" CACHE PATH
    "Directory with the bare-metal test programs built by test/build-firmware.sh")
  set(NUTTX_ELF "" CACHE FILEPATH "NuttX minion:nsh ELF for the NuttX boot test (optional)")
  # RISCV_FIRMWARE_TESTS=ON (acceptance builds): build the firmware with the
  # Docker toolchain as part of the build, rebuild it when its sources
  # change, register every firmware test and the objdump decoder cross-check,
  # and fail at configure time when Docker or Python is missing instead of
  # silently registering fewer tests.
  option(RISCV_FIRMWARE_TESTS "Build the firmware and require all firmware tests" OFF)
  find_package(Python3 COMPONENTS Interpreter)
  if(RISCV_FIRMWARE_TESTS)
    find_program(DOCKER_EXECUTABLE docker)
    if(NOT DOCKER_EXECUTABLE OR NOT Python3_FOUND)
      message(FATAL_ERROR "RISCV_FIRMWARE_TESTS needs Docker (image "
        "athrill-riscv-toolchain:ubuntu24, see tools/docker) and Python 3")
    endif()
    file(GLOB_RECURSE _firmware_sources CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/test/*.S" "${CMAKE_CURRENT_SOURCE_DIR}/test/*.ld"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/rv64i/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/test/rv64f/*.c"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/erbium/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/test/pmp/*.c"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/csr/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/test/fetch/*.c"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/mpu/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/test/common/*.h")
    add_custom_command(OUTPUT "${RISCV_FIRMWARE_DIR}/firmware.stamp"
      COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/test/build-firmware.sh" "${RISCV_FIRMWARE_DIR}"
      COMMAND "${CMAKE_COMMAND}" -E touch "${RISCV_FIRMWARE_DIR}/firmware.stamp"
      DEPENDS ${_firmware_sources} "${CMAKE_CURRENT_SOURCE_DIR}/test/build-firmware.sh"
      COMMENT "Building the bare-metal test firmware (Docker)")
    add_custom_target(riscv-firmware ALL DEPENDS "${RISCV_FIRMWARE_DIR}/firmware.stamp")
    add_test(NAME decoder-crosscheck-objdump COMMAND Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/test/crosscheck_decoder.py"
      --dump $<TARGET_FILE:athrill-decoder-dump>)
  endif()
  set(_runner "${CMAKE_CURRENT_SOURCE_DIR}/test/run_firmware_test.py")
  set(_erbium "${CMAKE_CURRENT_SOURCE_DIR}/test/erbium")
  if(Python3_FOUND AND (RISCV_FIRMWARE_TESTS OR EXISTS "${RISCV_FIRMWARE_DIR}/rv64i-selftest.elf"))
    add_test(NAME athrill-malformed-elf COMMAND Python3::Interpreter
      "${ATHRILL_SOURCE_DIR}/test/malformed_elf_test.py"
      --athrill $<TARGET_FILE:athrill3>
      --memory "${CMAKE_CURRENT_SOURCE_DIR}/test/rv64i/memory.txt"
      --elf "${RISCV_FIRMWARE_DIR}/rv64i-selftest.elf")
    add_test(NAME firmware-pmp-decode-cache COMMAND Python3::Interpreter "${_runner}"
      --athrill $<TARGET_FILE:athrill3>
      --memory "${CMAKE_CURRENT_SOURCE_DIR}/test/rv64i/memory.txt"
      --elf "${RISCV_FIRMWARE_DIR}/pmp-decode-cache-test.elf" --clocks 1000000)
    add_test(NAME firmware-csr-write COMMAND Python3::Interpreter "${_runner}"
      --athrill $<TARGET_FILE:athrill3>
      --memory "${CMAKE_CURRENT_SOURCE_DIR}/test/rv64i/memory.txt"
      --elf "${RISCV_FIRMWARE_DIR}/csr-write-test.elf" --clocks 1000000)
    add_test(NAME firmware-mpu-region COMMAND Python3::Interpreter "${_runner}"
      --athrill $<TARGET_FILE:athrill3>
      --memory "${CMAKE_CURRENT_SOURCE_DIR}/test/mpu/memory.txt"
      --elf "${RISCV_FIRMWARE_DIR}/mpu-region-test.elf" --clocks 1000000)
    add_test(NAME firmware-fetch-boundary COMMAND Python3::Interpreter "${_runner}"
      --athrill $<TARGET_FILE:athrill3>
      --memory "${CMAKE_CURRENT_SOURCE_DIR}/test/fetch/memory.txt"
      --elf "${RISCV_FIRMWARE_DIR}/fetch-boundary-test.elf" --clocks 1000000)
    if(RISCV_ERBIUM_COMPAT)
      add_test(NAME firmware-rv64i-selftest COMMAND Python3::Interpreter "${_runner}"
        --athrill $<TARGET_FILE:athrill3>
        --memory "${CMAKE_CURRENT_SOURCE_DIR}/test/rv64i/memory.txt"
        --elf "${RISCV_FIRMWARE_DIR}/rv64i-selftest.elf" --clocks 3000000)
      add_test(NAME firmware-rv64f-selftest COMMAND Python3::Interpreter "${_runner}"
        --athrill $<TARGET_FILE:athrill3>
        --memory "${CMAKE_CURRENT_SOURCE_DIR}/test/rv64f/memory.txt"
        --elf "${RISCV_FIRMWARE_DIR}/rv64f-selftest-erbium.elf" --clocks 3000000)
      add_test(NAME firmware-erbium-devices COMMAND Python3::Interpreter "${_runner}"
        --athrill $<TARGET_FILE:athrill3>
        --memory "${_erbium}/memory.txt" --device-config "${_erbium}/device_config.txt"
        --elf "${RISCV_FIRMWARE_DIR}/erbium-device-test.elf"
        --command "serial 0 Z" --command cont --timeout 30)
    else()
      add_test(NAME firmware-rv64f-selftest COMMAND Python3::Interpreter "${_runner}"
        --athrill $<TARGET_FILE:athrill3>
        --memory "${CMAKE_CURRENT_SOURCE_DIR}/test/rv64f/memory.txt"
        --elf "${RISCV_FIRMWARE_DIR}/rv64f-selftest-standard.elf" --clocks 3000000)
    endif()
  else()
    message(WARNING "Firmware tests are NOT registered: build them with "
      "test/build-firmware.sh ${RISCV_FIRMWARE_DIR} or configure with -DRISCV_FIRMWARE_TESTS=ON")
  endif()
  if(Python3_FOUND AND RISCV_ERBIUM_COMPAT AND NUTTX_ELF AND EXISTS "${NUTTX_ELF}")
    add_test(NAME firmware-nuttx-nsh COMMAND Python3::Interpreter "${_runner}"
      --athrill $<TARGET_FILE:athrill3>
      --memory "${_erbium}/memory.txt" --device-config "${_erbium}/device_config.txt"
      --elf "${NUTTX_ELF}" --command "serial 0 help\\n" --command cont
      --expect "Builtin Apps:" --timeout 60)
    # The same without the debugger (-t): the UART input comes from a file.
    file(READ "${_erbium}/device_config.txt" _erbium_device_config)
    file(WRITE "${CMAKE_BINARY_DIR}/nuttx-file-input-device-config.txt"
      "${_erbium_device_config}DEVICE_CONFIG_ERBIUM_UART_RX_FILE ${_erbium}/nsh_help_input.txt\n")
    add_test(NAME firmware-nuttx-nsh-file-input COMMAND Python3::Interpreter "${_runner}"
      --athrill $<TARGET_FILE:athrill3>
      --memory "${_erbium}/memory.txt"
      --device-config "${CMAKE_BINARY_DIR}/nuttx-file-input-device-config.txt"
      --elf "${NUTTX_ELF}" --clocks 10000000000 --expect "Builtin Apps:" --timeout 60)
  endif()
endif()

message(STATUS "Athrill target: RV64IMFC + Zicsr/Zifencei (Erbium compat: ${RISCV_ERBIUM_COMPAT})")
message(STATUS "Athrill EXDEV: ${ATHRILL_ENABLE_EXDEV}")
message(STATUS "Athrill Hakoniwa: ${ATHRILL_ENABLE_HAKONIWA}")
