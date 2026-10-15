# =============================================================================
# @file    CMakeLists.txt
# @brief   cortex-m-ota-boot 的 GCC/CMake 构建入口
#
# 与 Keil 工程并行存在，互不影响：同一份源码由两套构建各自的配置编译，产物
# 地址、分区、栈大小必须逐项一致（对照表见 docs）。
#
# 目标产物：
#   boot         Bootloader  链接于 0x08000000（sector 0-1，整块 32 KB）
#   app_slot_a   App（Slot A）链接于 0x08020200
#   app_slot_b   App（Slot B）链接于 0x08080200
#
# 用法：
#   cmake -B build/f407 -G Ninja -DBOARD=stm32f407_demo
#   cmake --build build/f407
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# 工具链文件默认就指向 arm-none-eabi，命令行不必再显式给出；已指定则以指定的为准
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake"
      CACHE FILEPATH "STM32F407 交叉编译工具链文件")
endif()

project(cortex_m_ota_boot C ASM)

set(BOARD "stm32f407_demo" CACHE STRING "目标板名称")
if(NOT BOARD STREQUAL "stm32f407_demo")
  message(FATAL_ERROR "不支持的 BOARD=${BOARD}，当前仅提供 stm32f407_demo")
endif()

set(OTA_ROOT      "${CMAKE_CURRENT_SOURCE_DIR}")
set(OTA_BOOT_DIR  "${OTA_ROOT}/Keil_OTA_Boot")
set(OTA_APP_DIR   "${OTA_ROOT}/Keil_App")
set(OTA_LINKER_DIR "${OTA_ROOT}/cmake/linker")
set(OTA_STARTUP   "${OTA_ROOT}/cmake/startup/startup_stm32f407xx.S")

# ---------------------------------------------------------------------------
# 共用配置
# ---------------------------------------------------------------------------

# Common/ota：与硬件无关的纯逻辑模块，Boot 与 App 编译的是同一份源文件
set(OTA_COMMON_SRC
    "${OTA_ROOT}/Common/ota/ota_cli.c"
    "${OTA_ROOT}/Common/ota/ota_crc32.c"
    "${OTA_ROOT}/Common/ota/ota_image.c"
    "${OTA_ROOT}/Common/ota/ota_request.c"
    "${OTA_ROOT}/Common/ota/ota_ymodem.c")

# STM32F4 HAL 源文件：CubeMX 生成的同一套驱动，两边各自复制了一份
set(OTA_HAL_SRC_NAMES
    stm32f4xx_hal.c
    stm32f4xx_hal_cortex.c
    stm32f4xx_hal_dma.c
    stm32f4xx_hal_dma_ex.c
    stm32f4xx_hal_exti.c
    stm32f4xx_hal_flash.c
    stm32f4xx_hal_flash_ex.c
    stm32f4xx_hal_flash_ramfunc.c
    stm32f4xx_hal_gpio.c
    stm32f4xx_hal_pwr.c
    stm32f4xx_hal_pwr_ex.c
    stm32f4xx_hal_rcc.c
    stm32f4xx_hal_rcc_ex.c
    stm32f4xx_hal_tim.c
    stm32f4xx_hal_tim_ex.c
    stm32f4xx_hal_uart.c)

# Boot 侧 CubeMX 已关掉 TIM2 与 USART2 的 TX/RX DMA：TIM 的源文件已从 Drivers 目录
# 移除，DMA 的源文件还在但 Boot 全仓零处引用。App 侧两者都仍在使用，所以这里按目标
# 各取一份，而不是把它们从共用列表里删掉
set(OTA_BOOT_HAL_SRC_NAMES ${OTA_HAL_SRC_NAMES})
list(REMOVE_ITEM OTA_BOOT_HAL_SRC_NAMES
    stm32f4xx_hal_tim.c
    stm32f4xx_hal_tim_ex.c
    stm32f4xx_hal_dma.c
    stm32f4xx_hal_dma_ex.c)

# -ffunction-sections/-fdata-sections 配合 --gc-sections 才能按函数粒度裁剪，
# 与 Keil 的段切分效果相当
set(OTA_C_FLAGS -std=c99 -ffunction-sections -fdata-sections -Wall)
set(OTA_LD_FLAGS -Wl,--print-memory-usage)

function(ota_configure_target target)
  target_compile_options(${target} PRIVATE ${OTA_C_FLAGS} ${ARGN})
  target_link_options(${target} PRIVATE ${OTA_LD_FLAGS})
  set_target_properties(${target} PROPERTIES
      SUFFIX ".elf"
      OUTPUT_NAME "${target}")
endfunction()

# 生成烧录用的 hex / bin，并打印段大小，便于和 Keil 的 Code/RO/RW/ZI 对照
function(ota_add_artifacts target)
  add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O ihex   "$<TARGET_FILE:${target}>" "${CMAKE_CURRENT_BINARY_DIR}/${target}.hex"
      COMMAND ${CMAKE_OBJCOPY} -O binary "$<TARGET_FILE:${target}>" "${CMAKE_CURRENT_BINARY_DIR}/${target}.bin"
      COMMAND ${CMAKE_SIZE} "$<TARGET_FILE:${target}>"
      COMMENT "生成 ${target}.hex / ${target}.bin")
endfunction()

# ---------------------------------------------------------------------------
# Bootloader
# ---------------------------------------------------------------------------

add_executable(boot
    ${OTA_STARTUP}
    ${OTA_BOOT_DIR}/Boot/Core/Src/boot_main.c
    ${OTA_BOOT_DIR}/Boot/Core/Src/boot_jump.c
    ${OTA_BOOT_DIR}/Core/Src/main.c
    ${OTA_BOOT_DIR}/Core/Src/gpio.c
    ${OTA_BOOT_DIR}/Core/Src/usart.c
    ${OTA_BOOT_DIR}/Core/Src/stm32f4xx_hal_msp.c
    ${OTA_BOOT_DIR}/Core/Src/stm32f4xx_it.c
    ${OTA_BOOT_DIR}/Core/Src/system_stm32f4xx.c
    ${OTA_BOOT_DIR}/Boot/Protocol/Src/boot_cli.c
    ${OTA_BOOT_DIR}/Boot/Protocol/Src/boot_transport.c
    ${OTA_BOOT_DIR}/Boot/Protocol/Src/boot_update.c
    ${OTA_BOOT_DIR}/Boot/Storage/Src/boot_adopt.c
    ${OTA_BOOT_DIR}/Boot/Storage/Src/boot_flash.c
    ${OTA_BOOT_DIR}/Boot/Storage/Src/boot_image.c
    ${OTA_BOOT_DIR}/Boot/Storage/Src/boot_metadata.c
    ${OTA_BOOT_DIR}/Boot/Storage/Src/boot_nvs.c
    ${OTA_BOOT_DIR}/Boot/Storage/Src/boot_slot_writer.c
    ${OTA_BOOT_DIR}/Boot/Mailbox/Src/boot_mailbox.c
    ${OTA_BOOT_DIR}/Boot/Crypto/Src/boot_pubkey.c
    ${OTA_BOOT_DIR}/Boot/Crypto/Src/boot_verify.c
    ${OTA_BOOT_DIR}/Boot/Crypto/monocypher/monocypher.c
    ${OTA_BOOT_DIR}/Boot/Crypto/monocypher/monocypher-ed25519.c
    ${OTA_COMMON_SRC})

foreach(name IN LISTS OTA_BOOT_HAL_SRC_NAMES)
  target_sources(boot PRIVATE "${OTA_BOOT_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/${name}")
endforeach()

target_include_directories(boot PRIVATE
    ${OTA_BOOT_DIR}/Core/Inc
    ${OTA_BOOT_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${OTA_BOOT_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${OTA_BOOT_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${OTA_BOOT_DIR}/Drivers/CMSIS/Include
    ${OTA_BOOT_DIR}/Boot/Config
    ${OTA_BOOT_DIR}/Boot/Core/Inc
    ${OTA_BOOT_DIR}/Boot/Storage/Inc
    ${OTA_BOOT_DIR}/Boot/Mailbox/Inc
    ${OTA_BOOT_DIR}/Boot/Protocol/Inc
    ${OTA_BOOT_DIR}/Boot/Crypto/Inc
    ${OTA_BOOT_DIR}/Boot/Crypto/monocypher
    ${OTA_ROOT}/Common/ota)

# OTA_BOOT_RAMCODE：只有 Boot 有 .boot_ramcode 段，启动文件据此决定搬不搬
target_compile_definitions(boot PRIVATE
    USE_HAL_DRIVER
    STM32F407xx
    OTA_BOOT_RAMCODE)

# Keil 侧 Optim=3（-O2）实测 Code+RO+RW = 31808 B，占 32 KB 分区的 97.07%；GCC +
# newlib-nano 若同为 -O2 会溢出 208 B，因为 nano 比 microlib 大约 1 KB。改用 -Os 压到
# 29280 B（89.36%），余量留给 v0.3 的 IWDG 与自动回滚
ota_configure_target(boot -Os)
target_link_options(boot PRIVATE -T "${OTA_LINKER_DIR}/boot.ld"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/boot.map")
# 显式声明链接脚本为依赖：否则改动 .ld 不会触发重链接，改了个寂寞
set_target_properties(boot PROPERTIES LINK_DEPENDS "${OTA_LINKER_DIR}/boot.ld")
ota_add_artifacts(boot)

# ---------------------------------------------------------------------------
# App（Slot A / Slot B 是同一份源码的两个链接版本）
# ---------------------------------------------------------------------------

function(ota_add_app_target target slot_define slot_ld)
  add_executable(${target}
      ${OTA_STARTUP}
      ${OTA_APP_DIR}/Core/Src/main.c
      ${OTA_APP_DIR}/Core/Src/dma.c
      ${OTA_APP_DIR}/Core/Src/usart.c
      ${OTA_APP_DIR}/Core/Src/tim.c
      ${OTA_APP_DIR}/Core/Src/gpio.c
      ${OTA_APP_DIR}/Core/Src/stm32f4xx_hal_msp.c
      ${OTA_APP_DIR}/Core/Src/stm32f4xx_it.c
      ${OTA_APP_DIR}/Core/Src/system_stm32f4xx.c
      ${OTA_APP_DIR}/App/Platform/Src/app_transport.c
      ${OTA_APP_DIR}/App/Platform/Src/app_flash.c
      ${OTA_APP_DIR}/App/Services/Src/app_cli.c
      ${OTA_APP_DIR}/App/Services/Src/app_nvs.c
      ${OTA_APP_DIR}/App/Services/Src/app_ota_service.c
      ${OTA_APP_DIR}/App/Services/Src/app_slot_writer.c
      ${OTA_APP_DIR}/App/BootClient/Src/app_boot_client.c
      ${OTA_COMMON_SRC})

  foreach(name IN LISTS OTA_HAL_SRC_NAMES)
    target_sources(${target} PRIVATE "${OTA_APP_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/${name}")
  endforeach()

  target_include_directories(${target} PRIVATE
      ${OTA_APP_DIR}/Core/Inc
      ${OTA_APP_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc
      ${OTA_APP_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
      ${OTA_APP_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
      ${OTA_APP_DIR}/Drivers/CMSIS/Include
      ${OTA_APP_DIR}/App/Config
      ${OTA_APP_DIR}/App/BootClient/Inc
      ${OTA_APP_DIR}/App/Platform/Inc
      ${OTA_APP_DIR}/App/Services/Inc
      ${OTA_ROOT}/Common/ota)

  # slot_define 决定 system_stm32f4xx.c 写进 VTOR 的偏移，必须与链接基址一致
  target_compile_definitions(${target} PRIVATE
      USE_HAL_DRIVER
      STM32F407xx
      ${slot_define})

  # Keil 侧 Optim=4（-O3）
  ota_configure_target(${target} -O3)
  target_link_options(${target} PRIVATE -T "${OTA_LINKER_DIR}/${slot_ld}"
      "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
  set_target_properties(${target} PROPERTIES LINK_DEPENDS "${OTA_LINKER_DIR}/${slot_ld}")
  ota_add_artifacts(${target})
endfunction()

ota_add_app_target(app_slot_a APP_SLOT_A app_slot_a.ld)
ota_add_app_target(app_slot_b APP_SLOT_B app_slot_b.ld)
