cmake_minimum_required(VERSION 3.16)
project(athrill3 VERSION 0.1.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CTest)
find_package(Threads REQUIRED)

set(ATHRILL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../athrill" CACHE PATH "Athrill core checkout")
get_filename_component(ATHRILL_SOURCE_DIR "${ATHRILL_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ATHRILL_ADDRESS_WIDTH 64 CACHE STRING "Athrill bus address width" FORCE)
include("${ATHRILL_SOURCE_DIR}/cmake/AthrillCore.cmake")
set(ATHRILL_TARGET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
option(ATHRILL_ENABLE_EXDEV "Enable external device loading" ON)

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
  src/device/device.c src/device/peripheral/erbium_sysreg.c src/device/peripheral/erbium_uart.c src/device/peripheral/erbium_timer.c
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
    src/device/device.c src/device/peripheral/erbium_sysreg.c src/device/peripheral/erbium_uart.c src/device/peripheral/erbium_timer.c
    src/device/intc/intc.c)
  riscv_target(athrill-clock-counter-test)
  add_test(NAME athrill-clock-counter COMMAND athrill-clock-counter-test)
  add_executable(athrill-decode-benchmark test/decode_benchmark.c
    ${RISCV_CPU_SOURCES})
  riscv_target(athrill-decode-benchmark)
  add_executable(athrill-decode-test test/decode_test.c)
  riscv_target(athrill-decode-test)
  target_link_libraries(athrill-decode-test PRIVATE athrill_riscv_cpu
    athrill_riscv_device athrill_bus athrill_mpu athrill_std
    athrill_riscv_config)
  add_test(NAME athrill-decode COMMAND athrill-decode-test)
endif()

message(STATUS "Athrill target: RV64IMC + Zicsr/Zifencei")
message(STATUS "Athrill EXDEV: ${ATHRILL_ENABLE_EXDEV}")
message(STATUS "Athrill Hakoniwa: ${ATHRILL_ENABLE_HAKONIWA}")
