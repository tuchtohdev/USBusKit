cmake_minimum_required(VERSION 3.20)

# --------------------------------------------------
# Cross-compilation configuration
# --------------------------------------------------

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m3)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(USBusKit_BluePill C ASM)

# --------------------------------------------------
# MCU configuration
# --------------------------------------------------

set(MCU_FLAGS
    -mcpu=cortex-m3
    -mthumb
)

# --------------------------------------------------
# CMSIS dependencies
# --------------------------------------------------

set(CMSIS_CORE
    ${CMAKE_SOURCE_DIR}/dependencies/CMSIS_6/CMSIS/Core/Include
)

set(STM32_CMSIS
    ${CMAKE_SOURCE_DIR}/dependencies/cmsis-device-f1/Include
)

set(STM32_STARTUP
    ${CMAKE_SOURCE_DIR}/dependencies/cmsis-device-f1/Source/Templates/gcc/startup_stm32f103xb.s
)

set(STM32_SYSTEM
    ${CMAKE_SOURCE_DIR}/dependencies/cmsis-device-f1/Source/Templates/system_stm32f1xx.c
)

# --------------------------------------------------
# TinyUSB 0.21.0
# --------------------------------------------------

set(TINYUSB_DIR
    ${CMAKE_SOURCE_DIR}/dependencies/tinyusb/src
)

set(TINYUSB_SOURCES
    # TinyUSB core
    ${TINYUSB_DIR}/tusb.c
    ${TINYUSB_DIR}/common/tusb_fifo.c

    # USB device stack
    ${TINYUSB_DIR}/device/usbd.c

    # CDC-Vendor class driver
    ${TINYUSB_DIR}/class/vendor/vendor_device.c

    # STM32F103 USB Full-Speed device controller
    ${TINYUSB_DIR}/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
    ${TINYUSB_DIR}/portable/st/stm32_fsdev/fsdev_common.c
)

# --------------------------------------------------
# Firmware executable
# --------------------------------------------------

add_executable(USBusKit.elf
    main.c
    usb_descriptors.c

    ${STM32_STARTUP}
    ${STM32_SYSTEM}

    ${TINYUSB_SOURCES}
)

# --------------------------------------------------
# Include directories
# --------------------------------------------------

target_include_directories(USBusKit.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMSIS_CORE}
    ${STM32_CMSIS}
    ${TINYUSB_DIR}
    ${CMAKE_SOURCE_DIR}
)

# --------------------------------------------------
# Preprocessor definitions
# --------------------------------------------------

target_compile_definitions(USBusKit.elf PRIVATE
    STM32F103xB
)

# --------------------------------------------------
# Compiler options
# --------------------------------------------------

target_compile_options(USBusKit.elf PRIVATE
    ${MCU_FLAGS}

    -Wall
    -Wextra

    -ffunction-sections
    -fdata-sections

    -g3
    -O0
)

# --------------------------------------------------
# Linker options
# --------------------------------------------------

target_link_options(USBusKit.elf PRIVATE
    ${MCU_FLAGS}

    -T${CMAKE_SOURCE_DIR}/STM32F103C8Tx_FLASH.ld

    -Wl,--gc-sections
    -Wl,-Map=USBusKit.map

    -specs=nosys.specs
)

# --------------------------------------------------
# Generate firmware binary
# --------------------------------------------------

add_custom_command(
    TARGET USBusKit.elf
    POST_BUILD

    COMMAND arm-none-eabi-objcopy
        -O binary
        $<TARGET_FILE:USBusKit.elf>
        ${CMAKE_BINARY_DIR}/USBusKit.bin

    COMMAND arm-none-eabi-size
        $<TARGET_FILE:USBusKit.elf>
)