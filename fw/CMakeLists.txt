cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)

include(pico_sdk_import.cmake)

project(rprom_project C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(rprom
  rprom.c
)

pico_set_linker_script(rprom ${CMAKE_CURRENT_LIST_DIR}/memmap_custom.ld)

pico_enable_stdio_usb(rprom 0)
pico_enable_stdio_uart(rprom 0)

pico_add_extra_outputs(rprom)

target_compile_definitions(rprom PRIVATE PICO_CORE1_STACK_SIZE=0)

target_link_libraries(rprom
  pico_stdlib
  pico_multicore
  hardware_flash
)
