cmake_minimum_required(VERSION 3.13)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(hl2io_board C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

add_executable(hl2io_board
    main.c
)

# Include the current directory for i2c_registers.h
target_include_directories(hl2io_board PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Pull in standard libraries and hardware support
target_link_libraries(hl2io_board
    pico_stdlib
    hardware_i2c
    pico_i2c_slave
)

# Enable console printing over both USB serial and GP0/GP1 UART
pico_enable_stdio_usb(hl2io_board 1)
pico_enable_stdio_uart(hl2io_board 1)

# Generate uf2 file for drag-and-drop programming
pico_add_extra_outputs(hl2io_board)
