/**
 ******************************************************************************
 * @file    app_slot_writer.h
 * @brief   非活动槽的流式写会话：整槽擦除 → 顺序写入 → 回读重算 CRC32
 *
 * 与 Boot 侧 boot_slot_writer 是同形实现（两个工程不共享源文件，各自编译）。
 * 三条硬约束一致：
 * 1. 目标只能是 Slot A / Slot B，且必须是非活动槽。擦掉正在运行的槽等于立刻
 *    砖化，App 自己就是最后一道防线；
 * 2. 每次写入的范围必须完整落在本次会话的槽内，Boot 区、Metadata 区、NVS 区
 *    与保留区都不在任何槽的区间里，天然被这一条挡掉；
 * 3. 只写 Flash，不碰 Metadata。App 没有写 Metadata 的权限，半写的槽不会被
 *    选中，因为 Boot 侧的 pending_slot 从未被提交过。
 ******************************************************************************
 */
#ifndef APP_SLOT_WRITER_H
#define APP_SLOT_WRITER_H

#include <stdbool.h>
#include <stdint.h>

/** 一次槽写入会话的运行状态，由调用方持有 */
typedef struct
{
    uint32_t payload_base;      /**< 载荷区起始地址（槽首 + 镜像头偏移） */
    uint32_t payload_capacity;  /**< 载荷区可用字节数（槽容量已扣除镜像头） */
    uint32_t image_size;        /**< 声明的镜像长度，单位字节 */
    uint32_t written;           /**< 已写入的镜像长度，单位字节 */
    bool is_active;             /**< 会话是否有效，关闭后一律拒绝写入 */
} app_slot_writer_t;

/**
 * @brief  开启写槽会话：校验目标槽后擦除整槽
 * @param  writer        会话对象，不可为 NULL
 * @param  slot_id       目标槽，必须是 Slot A / Slot B 且非活动槽
 * @param  running_slot  当前运行槽，取自 APP_SLOT_ID
 * @param  image_size    声明的镜像长度，必须落在 (0, 槽容量] 内
 * @return true 表示槽已擦净、可以开始写入
 * @retval false slot_id 非法、目标是运行槽、长度越界，或擦除失败
 * @note   擦 3 个 sector 期间 CPU 被 Flash 总线 stall 数百毫秒以上，因此调用方
 *         必须"先擦完再通知对端发包"：DMA 环形缓冲再大也顶不住对端在擦除期间
 *         持续发送。重复调用会先丢弃上一次未收尾的会话（幂等）。
 */
bool app_slot_writer_begin(app_slot_writer_t *writer,
                           uint32_t slot_id,
                           uint32_t running_slot,
                           uint32_t image_size);

/**
 * @brief  写入一段镜像数据
 * @param  writer 会话对象，不可为 NULL
 * @param  offset 本段数据在镜像内的偏移，必须等于已写入长度（只允许顺序写）
 * @param  data   待写入数据，不可为 NULL
 * @param  size   字节数，必须大于 0
 * @return true 表示已落盘
 * @retval false 会话未打开、参数非法，或偏移/长度越界
 * @note   只接受与已写入长度一致的偏移：跳跃写入会在槽里留空洞，而空洞无法由
 *         长度 + CRC32 检测出来（CRC 只覆盖声明长度）。
 */
bool app_slot_writer_write(app_slot_writer_t *writer,
                           uint32_t offset,
                           const uint8_t *data,
                           uint32_t size);

/**
 * @brief  回读本次会话声明长度内的数据并重算 CRC32
 * @param  writer    会话对象，不可为 NULL
 * @param  crc32_out 重算结果（全镜像口径，与 ota_crc32 的直接计算结果一致）
 * @return true 表示回读完成
 * @retval false 会话未打开、尚未收满，或回读失败
 * @note   与"边收边算"的 CRC32 互为印证：收包时算出的 CRC 只证明线上数据对，
 *         证明不了落盘对。省略这一步，Flash 编程错误要等 Boot 下次启动校验镜像
 *         时才暴露，那时只剩回滚一条路。
 */
bool app_slot_writer_verify(const app_slot_writer_t *writer, uint32_t *crc32_out);

/**
 * @brief  结束会话并清空会话状态
 * @param  writer 会话对象，允许为 NULL
 * @note   不擦除、不回滚已写入的数据。被 CAN 中止或超时放弃时，半写的槽留在
 *         原地即可（Boot 侧 Metadata 没被提交，它不会被选中）；但会话状态必须
 *         清干净，否则下一次升级会误以为槽还占着。
 */
void app_slot_writer_end(app_slot_writer_t *writer);

#endif /* APP_SLOT_WRITER_H */
