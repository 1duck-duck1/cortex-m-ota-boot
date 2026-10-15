/**
 ******************************************************************************
 * @file    boot.ld
 * @brief   Bootloader 的 GCC 链接脚本，分区与 boot_conf.h / Keil_OTA_Boot.sct 一致
 *
 * Flash：0x08000000 起 32 KB（sector 0-1），整块交给链接器排布。Keil 的 scatter
 * 把这块切成"31 KB 普通代码 + 1 KB RAM 函数初值副本"两段，GCC 侧直接合成一段：
 * 32 KB 才是真正不可越过的界，内部劈开只是把可用的余量钉死，反而更容易溢出。
 * 超限时链接器直接报 region overflow，不会悄悄挤掉别的东西。
 *
 * SRAM2 顶部 32 字节 0x2001FFE0 是 Boot 邮箱，两端都不分配：RAM2 区长度被截到
 * 邮箱起点为止。
 *
 * RAMCODE_RAM 声明成 (rw) 而非 (rwx)：GNU ld 只用区的属性决定 ELF 段权限，这里
 * 标 rwx 会让 .boot_ramcode 生成一个 RWX 段并触发告警。Cortex-M4 未启用 MPU，
 * SRAM 全域可执行，该段实际照常运行，故取与"启动时写入、运行时可执行"都相容的
 * 读写标记，避免那条恒真的告警。
 ******************************************************************************
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH       (rx)  : ORIGIN = 0x08000000, LENGTH = 0x00008000  /* Boot 全部分区，sector 0-1 */
  RAM         (rw)  : ORIGIN = 0x20000000, LENGTH = 0x0001B800  /* 主 SRAM，上界让给 RAM 函数 */
  RAMCODE_RAM (rw)  : ORIGIN = 0x2001B800, LENGTH = 0x00000400  /* 擦写函数的运行地址 */
  RAM2        (rw)  : ORIGIN = 0x2001BC00, LENGTH = 0x000043E0  /* SRAM2，尾部 32 字节是邮箱 */
}

/* 栈顶取主 SRAM 的上界：RAM 函数区在它之上，两者不相交 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* 与 Keil 侧的 STACK / HEAP 区域等价，从 RAM 高端往下预留 */
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x1000;

SECTIONS
{
  /* 向量表必须落在 Flash 首个字，复位后才取得到 MSP 与入口 */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  /* C 运行时的构造函数数组一律丢弃：这套固件的启动代码
     （cmake/startup/startup_stm32f407xx.S）不调用 __libc_init_array，没有静态
     构造函数要跑，这三个节恒为空。留着它们反而有害——其输入节带 WA 属性，落进
     Flash 会让整个 Flash LOAD 段变成 RWE（binutils 2.39 起会就此告警），不显式
     定义又会变成由链接器自行安放的孤儿节。 */
  /DISCARD/ :
  {
    *(.preinit_array*)
    *(.init_array*)
    *(.fini_array*)
  }

  /* Flash 擦写函数：运行地址在 SRAM，初值副本留在 Flash 里紧跟只读数据，
     由 Reset_Handler 在 main 之前搬运 */
  .boot_ramcode :
  {
    . = ALIGN(4);
    __ramcode_start = .;
    *(.boot_ramcode)
    *(.boot_ramcode*)
    . = ALIGN(4);
    __ramcode_end = .;
  } > RAMCODE_RAM AT> FLASH
  __ramcode_load_start = LOADADDR(.boot_ramcode);

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > RAM

  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM

  /* 邮箱保护区：两端都不得靠得太近 */
  ASSERT(ORIGIN(RAM2) + LENGTH(RAM2) == 0x2001FFE0, "RAM2 上界必须停在 Boot 邮箱之前")
  ASSERT(ORIGIN(RAM) + LENGTH(RAM) == ORIGIN(RAMCODE_RAM), "主 SRAM 与 RAM 函数区之间不能留缝")
  ASSERT(ORIGIN(RAMCODE_RAM) + LENGTH(RAMCODE_RAM) == ORIGIN(RAM2), "RAM 函数区与 SRAM2 必须相邻")
}
