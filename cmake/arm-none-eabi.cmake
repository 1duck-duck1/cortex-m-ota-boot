# =============================================================================
# @file    arm-none-eabi.cmake
# @brief   STM32F407（Cortex-M4F）的 arm-none-eabi 交叉编译工具链定义
#
# 这里的编译选项与 Keil 工程逐项对齐：单精度硬浮点、C99、代码区按函数切分。
# Boot 与 App 的差异（-O2 / -O3、microlib / 标准库）由各自 target 追加，不写在
# 工具链文件里，避免把某个 target 的选择强加给另一个。
#
# 单独使用时：
#   cmake -B build/f407 -G Ninja -DBOARD=stm32f407_demo \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
# =============================================================================

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_SIZE         arm-none-eabi-size)

# 交叉编译产物不能在本机运行，编译器自检退化为"只编译、不链接"
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# CPU 选项必须同时出现在编译与链接两侧，否则链接器拿到的是错误的架构属性
set(OTA_CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT   "${OTA_CPU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${OTA_CPU_FLAGS}")

# -e Reset_Handler：把复位入口显式指给启动文件。默认入口是 crt0 的 _start，
# 它一旦成为 --gc-sections 的根，就会把 main 与 C 库初始化链一并拖进镜像；
# 换成 Reset_Handler 后，crash 无关的启动代码会被整段裁掉。Keil 侧靠 scatter
# 的 RESET 段达成同一效果。
#
# -nostartfiles：干脆不链入 crt0。它的 _start 会引用 exit / atexit /
# __libc_init_array，把整条 newlib 的 stdio・exit 链（findfp、stdio、_close_r、
# _lseek_r、_read_r、_write_r、lock、reent、impure）一并拖进镜像；而这套固件的
# 启动代码已自行完成 .data/.bss 初始化，既无静态构造函数也无退出流程。裁掉后
# 既省出 ROM，也顺带消掉 libnosys 那四条"未实现"链接告警。
# 注意串接必须用 string(CONCAT)：把两个字符串直接写成 set 的多个实参会被当成
# CMake 列表，中间插进去的分号会原样传给 gcc。
string(CONCAT CMAKE_EXE_LINKER_FLAGS_INIT
    "${OTA_CPU_FLAGS} -specs=nano.specs -specs=nosys.specs -nostartfiles"
    " -Wl,--gc-sections -Wl,-e,Reset_Handler")
