/**
 ******************************************************************************
 * @file    app_slot_a.ld
 * @brief   App（Slot A）的 GCC 链接脚本，与 app_conf.h / Keil_App_Slot_A.sct 一致
 *
 * 槽首 0x08020000 起 512 字节是镜像头，只有 Boot 会写，App 从不碰；因此 App 从
 * 槽首 + 0x200 链接，保证 SystemInit 写进 VTOR 的地址（槽首 + 0x200）既落在自己
 * 的向量表上，又是 512 字节对齐的。
 *
 * SRAM2 顶部 32 字节 0x2001FFE0 是 Boot 邮箱，两端都不分配。
 ******************************************************************************
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08020200, LENGTH = 0x0005FE00  /* Slot A 去掉 512 字节镜像头 */
  RAM   (rw)  : ORIGIN = 0x20000000, LENGTH = 0x0001C000  /* 主 SRAM，栈顶在它的上界 */
  RAM2  (rw)  : ORIGIN = 0x2001C000, LENGTH = 0x00003FE0  /* SRAM2，尾部 32 字节是邮箱 */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

/* 与 Keil 侧的 STACK / HEAP 区域等价，从 RAM 高端往下预留 */
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;

SECTIONS
{
  /* 向量表必须落在 Flash 首个字，SystemInit 把 VTOR 指向槽首 + 0x200 后取的就是它 */
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

  /* 邮箱保护区：SRAM2 上界必须停在邮箱起点 */
  ASSERT(ORIGIN(RAM2) + LENGTH(RAM2) == 0x2001FFE0, "RAM2 上界必须停在 Boot 邮箱之前")
}
