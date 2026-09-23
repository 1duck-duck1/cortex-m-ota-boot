/**
 ******************************************************************************
 * @file    boot_slot_writer.h
 * @brief   目标槽的写入会话：擦除、顺序追加、边收边算 CRC32
 *
 * 一次 YMODEM 接收对应一个写入会话。会话内部维护"已写多少字节"与"累计
 * CRC32 中间值"，因此不需要把整包固件缓存在 RAM 里——Boot 只有 128 KB SRAM，
 * 384 KB 的镜像根本放不下，只能边收边写边算。
 *
 * 硬校验全部在这里：目标槽非法、目标槽就是受保护槽、长度越界、写指针越出
 * 槽容量，一律拒绝。上层状态机即使算错了偏移，也只能写坏目标槽而已，
 * 碰不到 Boot 区与 Metadata。
 ******************************************************************************
 */
#ifndef BOOT_SLOT_WRITER_H
#define BOOT_SLOT_WRITER_H

#include <stdbool.h>
#include <stdint.h>

/** 一次槽写入会话的运行状态 */
typedef struct
{
    uint32_t slot_id;       /**< 目标槽编号 */
    uint32_t base;          /**< 目标槽起始地址 */
    uint32_t expected_size; /**< 期望写入的载荷长度，单位字节 */
    uint32_t written;       /**< 已写入的载荷长度，单位字节 */
    uint32_t crc;           /**< 累计 CRC32 中间值，需经 ota_crc32_final() 收尾 */
    bool is_open;           /**< 会话是否有效，关闭后一律拒绝写入 */
} boot_slot_writer_t;

/**
 * @brief  开始一次写入会话：校验目标槽并擦除
 * @param  writer         会话对象，不可为 NULL
 * @param  slot_id        目标槽，BOOT_SLOT_ID_A / BOOT_SLOT_ID_B
 * @param  protected_slot 受保护槽，通常是本次运行/待运行的槽；传 BOOT_SLOT_ID_NONE
 *                        表示没有需要保护的槽
 * @param  expected_size  期望写入的载荷长度，单位字节，必须大于 0
 * @return true 表示擦除完成，会话可用
 * @retval false 目标槽非法、与受保护槽相同、长度越界，或擦除失败
 * @note   擦除耗时数百毫秒，期间串口收到的字节会丢。调用方必须在擦除完成后
 *         才发出 ACK 与下一个 'C'，让发送端在擦除期间保持静默。
 */
bool boot_slot_writer_begin(boot_slot_writer_t *writer,
                            uint32_t slot_id,
                            uint32_t protected_slot,
                            uint32_t expected_size);

/**
 * @brief  顺序追加一段载荷
 * @param  writer 会话对象，不可为 NULL
 * @param  data   数据指针
 * @param  size   字节数，必须大于 0
 * @return true 表示已落盘
 * @retval false 会话未打开、参数非法，或数据超出期望长度
 * @note   超出期望长度的部分会被截断（YMODEM 末包用 0x00 补齐到 128/1024 字节，
 *         这些填充字节既不属于载荷，也不该计入 CRC32）。
 */
bool boot_slot_writer_write(boot_slot_writer_t *writer, const uint8_t *data, uint32_t size);

/**
 * @brief  回读校验：重算已写入载荷的 CRC32 并与累计值比对
 * @param  writer    会话对象，不可为 NULL
 * @param  crc32_out 累计值的最终形式，允许为 NULL
 * @return true 表示回读重算与累计值一致
 * @note   这是提交 pending 之前的第一道闸门：边收边算只能证明"收到的字节没坏"，
 *         回读重算才能证明"写进 Flash 的字节没坏"。
 */
bool boot_slot_writer_verify(const boot_slot_writer_t *writer, uint32_t *crc32_out);

/**
 * @brief  结束会话并取回结果
 * @param  writer        会话对象，不可为 NULL
 * @param  image_size_out 实际写入的载荷长度，允许为 NULL
 * @param  crc32_out      累计 CRC32 的最终形式，允许为 NULL
 * @return true 表示载荷长度已达期望值，会话已关闭
 * @retval false 会话未打开或尚未收满，会话保持原状
 */
bool boot_slot_writer_finish(boot_slot_writer_t *writer,
                             uint32_t *image_size_out,
                             uint32_t *crc32_out);

/**
 * @brief  放弃会话
 * @param  writer 会话对象，允许为 NULL
 * @note   只关闭会话，不回滚已写入的内容——槽已经是半写的了，回滚需要再擦一次，
 *         而 Metadata 从未引用过它，直接放着等下次覆盖即可。
 */
void boot_slot_writer_abort(boot_slot_writer_t *writer);

/**
 * @brief  查询会话是否可写
 * @param  writer 会话对象
 * @return true 表示会话已打开
 */
bool boot_slot_writer_is_open(const boot_slot_writer_t *writer);

#endif /* BOOT_SLOT_WRITER_H */
