/**
 ******************************************************************************
 * @file    app_ota_service.c
 * @brief   App 侧升级子状态机
 *
 * 与 Boot 侧 boot_update.c 保持同形的三段式状态机与同一条原则：**边收边写**、
 * **两道校验都在递交之前**、**唯一改变"下次启动哪个槽"的动作由 Boot 完成**。
 * 差异只在收尾：把请求写进 NVS，而不是 mark_pending。
 ******************************************************************************
 */
#include "app_ota_service.h"

#include "app_config.h"
#include "app_flash.h"
#include "app_nvs.h"
#include "app_slot_writer.h"
#include "app_transport.h"

#include "ota_crc32.h"
#include "ota_image.h"
#include "ota_request.h"
#include "ota_ymodem.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>

/** 握手阶段周期性发送 'C' 的间隔 */
#define APP_OTA_C_INTERVAL_MS           1000u
/** 握手阶段等待第 0 包的静默超时 */
#define APP_OTA_HANDSHAKE_TIMEOUT_MS    15000u
/** 数据与收尾阶段的静默超时；115200 下 1024 字节包约 89 ms，5 s 足够宽松 */
#define APP_OTA_SILENCE_TIMEOUT_MS      5000u
/** 距最近一次成帧事件的上限，专门挡"一直在收字节但从不成帧"的持续噪声 */
#define APP_OTA_STALL_TIMEOUT_MS        30000u
/** 同一包连续重传出错上限，超过即放弃本次会话 */
#define APP_OTA_MAX_RETRY               10u
/** 单轮最多从环形缓冲取走的字节数 */
#define APP_OTA_READ_CHUNK              64u
/** 收尾阶段的结束包序号固定为 0 */
#define APP_OTA_END_PACKET_INDEX        0u

/** 升级子状态机的内部阶段 */
typedef enum
{
    APP_OTA_STATE_IDLE = 0,     /**< 空闲，串口归命令行 */
    APP_OTA_STATE_HANDSHAKE,    /**< 周期发 'C'，等文件信息包 */
    APP_OTA_STATE_RECEIVE,      /**< 逐包写槽并累计 CRC32 */
    APP_OTA_STATE_FINAL         /**< 等结束包，随后校验并递交 NVS 请求 */
} app_ota_state_t;

static ota_ymodem_rx_t s_rx;
static uint8_t s_read_buffer[APP_OTA_READ_CHUNK];
static app_ota_state_t s_state = APP_OTA_STATE_IDLE;
static app_ota_status_t s_status;
static uint32_t s_expected_index;
static uint32_t s_file_offset;          /**< 已累计的有效镜像字节数 */
static uint32_t s_crc_intermediate;     /**< CRC32 中间值，输出前还要异或一次 */
static uint32_t s_phase_tick;           /**< 本阶段最近一次收到字节的时刻 */
static uint32_t s_stall_tick;           /**< 最近一次成帧事件（PACKET/EOT/CAN/ERROR）的时刻 */
static uint32_t s_handshake_tick;       /**< 最近一次发送 'C' 的时刻 */
static uint32_t s_retry;
static uint32_t s_requested_version;
static bool s_is_version_requested;
static bool s_is_request_pending;

static void app_ota_send(uint8_t byte)
{
    if (app_transport_write(&byte, 1u))
    {
        return;
    }
}

/**
 * @brief  向量表合法性检查（与 Boot 侧 boot_image_vector_valid() 同一套判据）
 * @param  vector_base 向量表地址，即槽首 + OTA_IMAGE_LOAD_OFFSET
 * @param  size        载荷长度，用于限定入口地址上界
 * @return true 表示向量表处的 SP/PC 落在合法范围
 * @note   APP_RAM_END 是 SRAM 上界（不含），因此用 >= 判越界。
 */
static bool app_ota_vector_valid(uint32_t vector_base, uint32_t size)
{
    uint32_t sp;
    uint32_t pc;

    if ((vector_base == 0u) || (size < 8u))
    {
        return false;
    }

    sp = *(volatile uint32_t *)vector_base;
    pc = *(volatile uint32_t *)(vector_base + 4u);

    if (sp == 0xFFFFFFFFu)
    {
        return false;   /* 槽为空（擦除态） */
    }
    if ((sp < APP_RAM_BASE) || (sp >= APP_RAM_END))
    {
        return false;   /* 初始 MSP 必须落在 SRAM 内 */
    }
    if ((pc < vector_base) || (pc >= (vector_base + size)))
    {
        return false;   /* 入口必须落在本槽载荷区内 */
    }
    if ((pc & 1u) == 0u)
    {
        return false;   /* Thumb 入口地址最低位必须为 1 */
    }

    return true;
}

bool app_ota_service_slot_has_image(uint32_t slot_id)
{
    uint32_t size = app_flash_slot_size(slot_id);
    uint32_t base = app_flash_slot_base(slot_id);

    if ((size == 0u) || (base == 0u) || (size <= OTA_IMAGE_LOAD_OFFSET))
    {
        return false;
    }

    return app_ota_vector_valid(base + OTA_IMAGE_LOAD_OFFSET,
                                size - OTA_IMAGE_LOAD_OFFSET);
}

static app_ota_result_t app_ota_finish_with(app_ota_result_t result,
                                            app_ota_reason_t reason)
{
    s_status.result = result;
    s_status.reason = reason;

    return result;
}

/**
 * @brief  拒收本次升级并发 CAN 中止
 * @note   连发两个 CAN 是 YMODEM 规定的中止信号，只发一个会被对端当成噪声。
 *         拒绝的原因是"设备不认可这份声明"，与"链路出错"要区分开。
 */
static app_ota_result_t app_ota_reject(app_ota_reason_t reason)
{
    app_ota_send((uint8_t)OTA_YMODEM_CAN);
    app_ota_send((uint8_t)OTA_YMODEM_CAN);

    return app_ota_finish_with(APP_OTA_REJECTED, reason);
}

/**
 * @brief  回 NAK 请求重传，超过上限则中止会话
 */
static app_ota_result_t app_ota_retry_or_abort(void)
{
    app_ota_send((uint8_t)OTA_YMODEM_NAK);

    s_retry++;
    if (s_retry >= APP_OTA_MAX_RETRY)
    {
        return app_ota_finish_with(APP_OTA_ABORTED,
                                   APP_OTA_REASON_RETRY_EXCEEDED);
    }

    return APP_OTA_BUSY;
}

/**
 * @brief  目标槽 = 非活动槽
 * @note   目标槽由设备推导，上位机不传：写正在运行的槽等于立刻砖化，
 *         这层判断放在设备侧才不会被上位机的错误参数绕过。
 */
static uint32_t app_ota_select_target(void)
{
    return (APP_SLOT_ID == APP_SLOT_ID_A) ? APP_SLOT_ID_B : APP_SLOT_ID_A;
}

/**
 * @brief  处理第 0 包（文件信息包）：定版本、定目标槽、定长度
 */
static app_ota_result_t app_ota_handle_file_info(
    const ota_ymodem_packet_t *packet)
{
    ota_ymodem_file_info_t info;
    uint32_t version;

    if (packet->index != 0u)
    {
        /* 数据包先于文件信息包到达：本会话无法确定镜像身份。 */
        return app_ota_reject(APP_OTA_REASON_SEQUENCE_ERROR);
    }

    if (!ota_ymodem_parse_file_info(packet->data, packet->size, &info))
    {
        return app_ota_reject(APP_OTA_REASON_FILE_INFO_INVALID);
    }

    if ((info.file_size == 0u) ||
        (info.file_size > (APP_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET)))
    {
        /* 0 长度是"空批次"，超容量写不进载荷区，两者都不接受。 */
        return app_ota_reject(APP_OTA_REASON_SIZE_INVALID);
    }

    /* 版本优先级：命令行参数 > 第 0 包声明 > 拒绝。设备不允许"未知版本"进入
       Pending，否则回滚保护与将来的防回滚都失去依据。 */
    if (s_is_version_requested)
    {
        version = s_requested_version;
    }
    else if (info.has_version)
    {
        version = info.version;
    }
    else
    {
        return app_ota_reject(APP_OTA_REASON_VERSION_MISSING);
    }

    s_status.target_slot = app_ota_select_target();

    /* 先擦槽，再回 ACK + 'C'。擦除 3 个 sector 期间 CPU 被 Flash 总线 stall，
       而 DMA 环形缓冲只有 4 KB（115200 下约 355 ms），对端若已开始发包，
       溢出后丢掉的数据无法还原。代价是对端要多等一次擦除时间。 */
    if (!app_slot_writer_begin(s_status.target_slot, APP_SLOT_ID,
                               info.file_size))
    {
        return app_ota_reject(APP_OTA_REASON_SLOT_INVALID);
    }

    s_status.version = version;
    s_status.size = info.file_size;
    s_status.crc32 = OTA_CRC32_INIT_VALUE ^ OTA_CRC32_XOROUT_VALUE;

    s_file_offset = 0u;
    s_crc_intermediate = OTA_CRC32_INIT_VALUE;
    s_expected_index = 1u;
    s_retry = 0u;

    app_ota_send((uint8_t)OTA_YMODEM_ACK);
    app_ota_send((uint8_t)OTA_YMODEM_C);   /* 请求第 1 个数据包 */
    s_state = APP_OTA_STATE_RECEIVE;

    /* 计时基准从"擦完并通知对端"重新起算：擦除耗时不是线上静默，若沿用进入本
       函数前的时刻，擦得慢一点就会刚发完 'C' 立刻判超时。 */
    s_phase_tick = HAL_GetTick();

    return APP_OTA_BUSY;
}

/**
 * @brief  处理数据包：按序号去重后写槽，并累计长度与 CRC32
 * @note   只写落在声明长度内的字节：标准发送方会用 0x1A 把最后一包补齐，
 *         填充字节不属于镜像，写进去就会与上位机算出的 CRC32 对不上。
 */
static app_ota_result_t app_ota_handle_data(const ota_ymodem_packet_t *packet)
{
    uint32_t chunk;

    if (packet->index != s_expected_index)
    {
        if (((packet->index + 1u) & 0xFFu) == s_expected_index)
        {
            /* 重复帧（我们的 ACK 丢了导致对端重传）：补发 ACK，不重复写、
               不重复累计，否则对端重传一次就让 CRC32 永久失配。 */
            app_ota_send((uint8_t)OTA_YMODEM_ACK);
            return APP_OTA_BUSY;
        }
        return app_ota_reject(APP_OTA_REASON_SEQUENCE_ERROR);
    }

    chunk = s_status.size - s_file_offset;
    if (chunk > packet->size)
    {
        chunk = packet->size;
    }
    if (chunk == 0u)
    {
        /* 声明长度已经收满却又来一包：长度声明与实际内容矛盾。 */
        return app_ota_reject(APP_OTA_REASON_SIZE_MISMATCH);
    }

    if (!app_slot_writer_write(s_file_offset, packet->data, chunk))
    {
        return app_ota_finish_with(APP_OTA_ABORTED,
                                   APP_OTA_REASON_FLASH_FAILED);
    }

    s_crc_intermediate = ota_crc32_update(s_crc_intermediate, packet->data, chunk);
    s_file_offset += chunk;
    s_status.crc32 = s_crc_intermediate ^ OTA_CRC32_XOROUT_VALUE;

    /* 序号是 8 位，384 KB 镜像用 128 字节包也要 3072 包，回绕是正常现象。 */
    s_expected_index = (s_expected_index + 1u) & 0xFFu;
    s_retry = 0u;

    app_ota_send((uint8_t)OTA_YMODEM_ACK);

    return APP_OTA_BUSY;
}

/**
 * @brief  处理结束包（收尾）：校验落盘结果与向量表，然后递交 NVS 请求
 * @note   顺序不可颠倒：先回 ACK 让对端收工，再校验与落盘。两道校验都通过才
 *         写 NVS；任何一步失败都只报告错误，NVS 里不留半截请求，旧固件照常启动。
 */
static app_ota_result_t app_ota_handle_final(const ota_ymodem_packet_t *packet)
{
    ota_ymodem_file_info_t info;
    uint32_t flash_crc32;
    uint32_t base;

    if ((packet->index != APP_OTA_END_PACKET_INDEX) ||
        !ota_ymodem_parse_file_info(packet->data, packet->size, &info) ||
        (info.file_size != 0u) || (info.file_name[0] != '\0'))
    {
        /* 形状不对：说明 EOT 提前到达，数据阶段其实还没结束。 */
        return app_ota_reject(APP_OTA_REASON_SEQUENCE_ERROR);
    }

    /* 先 ACK 再干重活：后面的全槽回读要花数百毫秒，不能让对端停在等 ACK。 */
    app_ota_send((uint8_t)OTA_YMODEM_ACK);

    /* 校验一：回读槽内数据重算 CRC32。收包时算的 CRC 只证明"线上数据对"，
       证明不了"落盘对"；两者一致才排除了 Flash 编程错误。 */
    if (!app_slot_writer_verify(&flash_crc32))
    {
        return app_ota_finish_with(APP_OTA_ABORTED, APP_OTA_REASON_FLASH_FAILED);
    }
    if (flash_crc32 != s_status.crc32)
    {
        return app_ota_reject(APP_OTA_REASON_CRC_MISMATCH);
    }

    /* 校验二：向量表的 SP/PC 范围。挡在递交之前，否则一个永远起不来的镜像会
       被 Boot 写成 Pending，只能等连续启动计数超限后回滚——代价是一次完整的
       失败启动周期，而且用户会以为升级成功了。 */
    base = app_flash_slot_base(s_status.target_slot) + OTA_IMAGE_LOAD_OFFSET;
    if (!app_ota_vector_valid(base, s_status.size))
    {
        return app_ota_reject(APP_OTA_REASON_IMAGE_INVALID);
    }

    if (!app_nvs_write_request(s_status.version, s_status.target_slot,
                               s_status.size, s_status.crc32))
    {
        return app_ota_finish_with(APP_OTA_ABORTED, APP_OTA_REASON_NVS_FAILED);
    }

    app_slot_writer_end();

    return app_ota_finish_with(APP_OTA_DONE, APP_OTA_REASON_NONE);
}

/**
 * @brief  处理 EOT：数据阶段结束，转入收尾
 * @note   对第一个 EOT 直接回 ACK 而不按教科书写法先回 NAK：先 NAK 需要发送方
 *         重发 EOT 才能继续，而部分发送方只发一次 EOT，会直接把会话拖到超时。
 */
static app_ota_result_t app_ota_handle_eot(void)
{
    if (s_state == APP_OTA_STATE_HANDSHAKE)
    {
        /* 还没收到文件信息包就要结束：对端没有文件可发。 */
        app_ota_send((uint8_t)OTA_YMODEM_ACK);
        return app_ota_finish_with(APP_OTA_ABORTED,
                                   APP_OTA_REASON_FILE_INFO_INVALID);
    }

    if (s_state == APP_OTA_STATE_FINAL)
    {
        /* 收尾阶段重复到达的 EOT：ACK 掉即可，继续等结束包。 */
        app_ota_send((uint8_t)OTA_YMODEM_ACK);
        return APP_OTA_BUSY;
    }

    if (s_file_offset != s_status.size)
    {
        return app_ota_reject(APP_OTA_REASON_SIZE_MISMATCH);
    }

    /* 先 ACK 再发 'C'：标准发送方据此推送结束包。 */
    app_ota_send((uint8_t)OTA_YMODEM_ACK);
    app_ota_send((uint8_t)OTA_YMODEM_C);
    s_state = APP_OTA_STATE_FINAL;

    return APP_OTA_BUSY;
}

static app_ota_result_t app_ota_handle_packet(const ota_ymodem_packet_t *packet)
{
    switch (s_state)
    {
        case APP_OTA_STATE_HANDSHAKE:
            return app_ota_handle_file_info(packet);

        case APP_OTA_STATE_RECEIVE:
            return app_ota_handle_data(packet);

        case APP_OTA_STATE_FINAL:
            return app_ota_handle_final(packet);

        default:
            return APP_OTA_BUSY;
    }
}

static app_ota_result_t app_ota_handle_event(ota_ymodem_event_t event)
{
    /* 成帧事件才算"会话有进展"，是 APP_OTA_STALL_TIMEOUT_MS 的计时基准：
       退化帧、纯噪声字节都不算，否则噪声会把这道闸也一起顶掉。 */
    s_stall_tick = HAL_GetTick();

    switch (event)
    {
        case OTA_YMODEM_EVENT_CAN:
            return app_ota_finish_with(APP_OTA_ABORTED,
                                       APP_OTA_REASON_CANCELLED);

        case OTA_YMODEM_EVENT_ERROR:
            return app_ota_retry_or_abort();

        case OTA_YMODEM_EVENT_EOT:
            return app_ota_handle_eot();

        case OTA_YMODEM_EVENT_PACKET:
            return app_ota_handle_packet(&s_rx.packet);

        default:
            return APP_OTA_BUSY;
    }
}

/**
 * @brief  对端静默超时检查
 * @note   第一道闸的计时基准是"最近一次收到任何字节"，而不是"上一条完整包"：
 *         一包 1024 字节在 115200 下要传约 89 ms，按包计时会误判在传的包。
 *         代价是它挡不住"一直在收字节"的持续噪声（例如对端卡在重发 'C' 循环）：
 *         噪声会不断刷新基准，会话就此永久占住串口，命令行也不可用。
 *         因此补第二道闸——"距最近一次成帧事件"的上限。正常重传期间每个包都会
 *         产生 PACKET 或 ERROR，30 s 一次都没有只可能是噪声或对端已失控。
 *         该上限比握手期静默超时（15 s）宽松，不会越过第一道闸抢先报错。
 */
static app_ota_result_t app_ota_check_timeout(void)
{
    uint32_t timeout = (s_state == APP_OTA_STATE_HANDSHAKE) ?
                       APP_OTA_HANDSHAKE_TIMEOUT_MS :
                       APP_OTA_SILENCE_TIMEOUT_MS;

    if ((HAL_GetTick() - s_phase_tick) >= timeout)
    {
        return app_ota_finish_with(APP_OTA_ABORTED, APP_OTA_REASON_TIMEOUT);
    }

    if ((HAL_GetTick() - s_stall_tick) >= APP_OTA_STALL_TIMEOUT_MS)
    {
        return app_ota_finish_with(APP_OTA_ABORTED, APP_OTA_REASON_STALLED);
    }

    return APP_OTA_BUSY;
}

/**
 * @brief  泵一次会话：取字节 → 解码 → 处理事件 → 查超时 → （握手期）发 'C'
 */
static app_ota_result_t app_ota_pump(void)
{
    app_ota_result_t result = APP_OTA_BUSY;
    ota_ymodem_event_t event;
    uint32_t now = HAL_GetTick();
    uint32_t count;
    uint32_t index;

    count = app_transport_read(s_read_buffer, (uint32_t)sizeof(s_read_buffer));
    if (count > 0u)
    {
        s_phase_tick = now;
    }

    for (index = 0u; index < count; index++)
    {
        event = ota_ymodem_rx_feed(&s_rx, s_read_buffer[index]);
        if (event == OTA_YMODEM_EVENT_NONE)
        {
            continue;
        }

        result = app_ota_handle_event(event);
        if (result != APP_OTA_BUSY)
        {
            return result;
        }
    }

    result = app_ota_check_timeout();
    if (result != APP_OTA_BUSY)
    {
        return result;
    }

    if ((s_state == APP_OTA_STATE_HANDSHAKE) &&
        ((now - s_handshake_tick) >= APP_OTA_C_INTERVAL_MS))
    {
        s_handshake_tick = now;
        app_ota_send((uint8_t)OTA_YMODEM_C);
    }

    return APP_OTA_BUSY;
}

/**
 * @brief  结束会话，串口交还命令行
 * @note   被 CAN 中止或超时放弃时也要关掉写槽会话：半写的槽留在原地即可
 *         （NVS 里没有请求，Boot 不会选中它），但会话状态必须清干净，
 *         否则下一次升级会误以为槽还占着。
 */
static void app_ota_stop(void)
{
    app_slot_writer_end();

    s_state = APP_OTA_STATE_IDLE;
    s_is_request_pending = false;
    s_is_version_requested = false;
}

void app_ota_service_request(uint32_t version, bool has_version)
{
    s_requested_version = version;
    s_is_version_requested = has_version;
    s_is_request_pending = true;
}

bool app_ota_service_is_active(void)
{
    /* 已登记但还没开跑的请求也算"占用串口"：下一轮 app_ota_service_run() 就要
       接管字节流，此刻再让命令行去取字节，两者会互相抢。 */
    return (s_state != APP_OTA_STATE_IDLE) || s_is_request_pending;
}

app_ota_result_t app_ota_service_run(void)
{
    app_ota_result_t result;

    if (s_state == APP_OTA_STATE_IDLE)
    {
        if (!s_is_request_pending)
        {
            return APP_OTA_BUSY;
        }

        ota_ymodem_rx_init(&s_rx);

        s_state = APP_OTA_STATE_HANDSHAKE;
        s_retry = 0u;
        s_file_offset = 0u;
        s_crc_intermediate = OTA_CRC32_INIT_VALUE;
        s_expected_index = 1u;
        s_phase_tick = HAL_GetTick();
        s_stall_tick = s_phase_tick;
        s_handshake_tick = s_phase_tick;
        s_is_request_pending = false;

        s_status.target_slot = app_ota_select_target();
        s_status.version = OTA_REQUEST_VERSION_NONE;
        s_status.size = 0u;
        s_status.crc32 = OTA_CRC32_INIT_VALUE ^ OTA_CRC32_XOROUT_VALUE;
        s_status.result = APP_OTA_BUSY;
        s_status.reason = APP_OTA_REASON_NONE;

        app_ota_send((uint8_t)OTA_YMODEM_C);
    }

    result = app_ota_pump();
    if (result != APP_OTA_BUSY)
    {
        app_ota_stop();
    }

    return result;
}

const app_ota_status_t *app_ota_service_status(void)
{
    return &s_status;
}

const char *app_ota_reason_name(app_ota_reason_t reason)
{
    switch (reason)
    {
        case APP_OTA_REASON_NONE:
            return "NONE";
        case APP_OTA_REASON_CANCELLED:
            return "CANCELLED";
        case APP_OTA_REASON_TIMEOUT:
            return "TIMEOUT";
        case APP_OTA_REASON_STALLED:
            return "STALLED";
        case APP_OTA_REASON_RETRY_EXCEEDED:
            return "RETRY_EXCEEDED";
        case APP_OTA_REASON_FILE_INFO_INVALID:
            return "FILE_INFO_INVALID";
        case APP_OTA_REASON_VERSION_MISSING:
            return "VERSION_MISSING";
        case APP_OTA_REASON_SIZE_INVALID:
            return "SIZE_INVALID";
        case APP_OTA_REASON_SIZE_MISMATCH:
            return "SIZE_MISMATCH";
        case APP_OTA_REASON_SEQUENCE_ERROR:
            return "SEQUENCE_ERROR";
        case APP_OTA_REASON_SLOT_INVALID:
            return "SLOT_INVALID";
        case APP_OTA_REASON_FLASH_FAILED:
            return "FLASH_FAILED";
        case APP_OTA_REASON_CRC_MISMATCH:
            return "CRC_MISMATCH";
        case APP_OTA_REASON_IMAGE_INVALID:
            return "IMAGE_INVALID";
        case APP_OTA_REASON_NVS_FAILED:
            return "NVS_FAILED";
        default:
            return "UNKNOWN";
    }
}
