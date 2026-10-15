/**
 ******************************************************************************
 * @file    boot_update.c
 * @brief   OTA 接收状态机的实现
 ******************************************************************************
 */
#include "boot_update.h"

#include "boot_conf.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_transport.h"
#include "boot_verify.h"
#include "ota_image.h"
#include "stm32f4xx_hal.h"

#include <string.h>

/** 握手期用于取消会话的按键，与命令行的 Ctrl-C 保持一致 */
#define BOOT_UPDATE_KEY_CANCEL     (0x03u)

/** 包序号比较用的掩码，长镜像必然回绕 */
#define BOOT_UPDATE_INDEX_MASK     (0xFFu)

/**
 * @brief  进入某个阶段并重置该阶段的两个计时基点
 * @param  update 会话对象
 * @param  state  新阶段
 */
static void boot_update_enter(boot_update_t *update, boot_update_state_t state)
{
    uint32_t now = HAL_GetTick();

    update->state = state;
    update->state_tick = now;
    update->progress_tick = now;
}

/**
 * @brief  终止会话
 * @param  update 会话对象
 * @param  reason 终止原因
 */
static void boot_update_fail(boot_update_t *update, boot_update_reason_t reason)
{
    boot_slot_writer_abort(&update->writer);

    update->reason = reason;
    update->state = BOOT_UPDATE_STATE_FAILED;
}

/**
 * @brief  处理第 0 包：定版本、定长度、擦槽、回 ACK 与 'C'
 * @param  update 会话对象
 * @note   擦除必须发生在回 ACK 之前：擦一个 128 KB 扇区要几百毫秒，这段时间
 *         里串口收到的字节会整批丢掉。先让对端看到 ACK，它才会等我们发 'C'。
 */
static void boot_update_handle_file_info(boot_update_t *update)
{
    ota_ymodem_file_info_t info;

    if (update->rx.packet.index != 0u) {
        boot_update_fail(update, BOOT_UPDATE_REASON_BAD_SIZE);
        return;
    }

    if (!ota_ymodem_parse_file_info(update->rx.packet.data, update->rx.packet.size, &info)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_BAD_SIZE);
        return;
    }

    /* 版本优先级：命令行给的 > 第 0 包里的版本 > 拒绝提交。 */
    if (!update->has_version && info.has_version) {
        update->version = info.version;
        update->has_version = true;
    }
    if (!update->has_version) {
        boot_update_fail(update, BOOT_UPDATE_REASON_NO_VERSION);
        return;
    }

    if ((info.file_size == 0u) ||
        (info.file_size > (BOOT_SLOT_SIZE - OTA_IMAGE_LOAD_OFFSET))) {
        boot_update_fail(update, BOOT_UPDATE_REASON_BAD_SIZE);
        return;
    }

    /* 第 0 包携带的签名先存进会话：收尾验签只能用这里存下来的那一份。 */
    update->has_signature = info.has_signature;
    update->sig_algorithm = info.sig_algorithm;
    update->pubkey_id = info.pubkey_id;
    (void)memcpy(update->signature, info.signature, sizeof(update->signature));

#if (BOOT_REQUIRE_SIGNATURE != 0u)
    /* 提前拒签：在这里拒绝就不用白擦一遍 384 KB 的目标槽。 */
    if (!info.has_signature) {
        boot_update_fail(update, BOOT_UPDATE_REASON_SIGNATURE_FAILED);
        return;
    }
#endif

    (void)memcpy(update->file_name, info.file_name, sizeof(update->file_name));

    if ((update->protected_slot != BOOT_SLOT_ID_NONE) &&
        (update->target_slot == update->protected_slot)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_BAD_SLOT);
        return;
    }

    if (!boot_slot_writer_begin(&update->writer,
                                update->target_slot,
                                update->protected_slot,
                                info.file_size)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_ERASE_FAILED);
        return;
    }

    boot_transport_write_byte(OTA_YMODEM_ACK);
    boot_transport_write_byte(OTA_YMODEM_C);

    update->expected_index = 1u;
    update->retry_count = 0u;

    boot_update_enter(update, BOOT_UPDATE_STATE_RECEIVE);
}

/**
 * @brief  处理一个数据包
 * @param  update 会话对象
 */
static void boot_update_handle_packet(boot_update_t *update)
{
    uint32_t index = (uint32_t)update->rx.packet.index;

    /* 期望的包：写入并累计 CRC32。 */
    if (index == (update->expected_index & BOOT_UPDATE_INDEX_MASK)) {
        if (!boot_slot_writer_write(&update->writer,
                                    update->rx.packet.data,
                                    (uint32_t)update->rx.packet.size)) {
            boot_update_fail(update, BOOT_UPDATE_REASON_WRITE_FAILED);
            return;
        }

        boot_transport_write_byte(OTA_YMODEM_ACK);

        update->expected_index++;
        update->retry_count = 0u;
        update->progress_tick = HAL_GetTick();
        return;
    }

    /* 上一包的重传：ACK 但绝不重复写入，否则同一个包会被写两遍、CRC 也会翻倍。 */
    if (index == ((update->expected_index - 1u) & BOOT_UPDATE_INDEX_MASK)) {
        boot_transport_write_byte(OTA_YMODEM_ACK);
        update->progress_tick = HAL_GetTick();
        return;
    }

    boot_transport_write_byte(OTA_YMODEM_NAK);
    ota_ymodem_rx_init(&update->rx);
    update->retry_count++;
    if (update->retry_count > BOOT_OTA_MAX_RETRY) {
        boot_update_fail(update, BOOT_UPDATE_REASON_RETRY_EXCEEDED);
    }
}

/**
 * @brief  收尾：回读复核、补镜像头、提交 pending
 * @param  update 会话对象
 * @note   三道复核缺一不可：边收边算的 CRC32 只能证明"线上没坏"，回读重算
 *         才能证明"写进 Flash 的字节没坏"；向量表预检挡住那些 CRC 对得上却
 *         根本跑不起来的镜像；验签则证明这份镜像出自持有私钥的授权方。
 */
static void boot_update_finalize(boot_update_t *update)
{
    uint32_t image_size = 0u;
    uint32_t crc32 = 0u;
    uint32_t base;
    ota_image_header_t verify_header;

    if (!boot_slot_writer_verify(&update->writer, &crc32)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_VERIFY_FAILED);
        return;
    }

    base = boot_flash_slot_base(update->target_slot);
    if (!boot_image_vector_valid(base + OTA_IMAGE_LOAD_OFFSET,
                                 base,
                                 update->writer.written,
                                 NULL,
                                 NULL)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_VERIFY_FAILED);
        return;
    }

    if (!boot_slot_writer_finish(&update->writer, &image_size, &crc32)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_VERIFY_FAILED);
        return;
    }

    /* 验签：必须发生在补镜像头与 mark_pending 之前。用第 0 包存下的签名与设备
       常量重建 prologue，与 PC 侧构造逐字节对齐，再流式扫描槽内载荷求摘要验签。 */
    (void)memset(&verify_header, 0, sizeof(verify_header));
    verify_header.magic = OTA_IMAGE_MAGIC;
    verify_header.hw_id = OTA_IMAGE_HW_ID;
    verify_header.header_version = OTA_IMAGE_HEADER_VERSION;
    verify_header.header_size = OTA_IMAGE_HEADER_SIZE;
    verify_header.load_offset = OTA_IMAGE_LOAD_OFFSET;
    verify_header.image_size = image_size;
    verify_header.payload_crc32 = crc32;
    verify_header.version = update->version;
    /* 必须有这两行：verify_header 是本地重建的，memset 后 sig_algorithm 为 0，
       而 boot_verify_image() 会把 0 当成"未签名"当场拒绝，导致签名明明正确也报
       SIGNATURE_FAILED。这两个字段在 28 字节前缀之外，不影响被签哈希。 */
    verify_header.sig_algorithm = update->sig_algorithm;
    verify_header.pubkey_id = update->pubkey_id;
    if (!boot_verify_image(base, &verify_header, update->signature, update->pubkey_id)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_SIGNATURE_FAILED);
        return;
    }

    if (!boot_image_write_header(update->target_slot,
                                 update->version,
                                 image_size,
                                 crc32,
                                 update->sig_algorithm,
                                 update->pubkey_id,
                                 update->signature)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_WRITE_FAILED);
        return;
    }

    if (boot_metadata_mark_pending(update->metadata, update->target_slot)) {
        if (!boot_metadata_save(update->metadata)) {
            boot_update_fail(update, BOOT_UPDATE_REASON_COMMIT_FAILED);
            return;
        }
    }

    update->image_size = image_size;
    update->image_crc32 = crc32;
    update->reason = BOOT_UPDATE_REASON_NONE;
    update->state = BOOT_UPDATE_STATE_DONE;
}

/**
 * @brief  处理一个线上事件
 * @param  update 会话对象
 * @param  event  事件
 */
static void boot_update_handle_event(boot_update_t *update, ota_ymodem_event_t event)
{
    switch (event) {
        case OTA_YMODEM_EVENT_PACKET:
            if (update->state == BOOT_UPDATE_STATE_HANDSHAKE) {
                boot_update_handle_file_info(update);
            } else if (update->state == BOOT_UPDATE_STATE_RECEIVE) {
                boot_update_handle_packet(update);
            } else {
                /* 收尾阶段的包一律丢弃：会话已经不再接受载荷。 */
            }
            break;

        case OTA_YMODEM_EVENT_EOT:
            if (update->state == BOOT_UPDATE_STATE_HANDSHAKE) {
                boot_transport_write_byte(OTA_YMODEM_NAK);
            } else if (update->state == BOOT_UPDATE_STATE_RECEIVE) {
                boot_transport_write_byte(OTA_YMODEM_ACK);
                boot_update_enter(update, BOOT_UPDATE_STATE_FINAL);
                boot_update_finalize(update);
            } else {
                /* 重复的 EOT 只回 ACK，不再推进状态。 */
                boot_transport_write_byte(OTA_YMODEM_ACK);
            }
            break;

        case OTA_YMODEM_EVENT_CAN:
            boot_update_fail(update, BOOT_UPDATE_REASON_CANCELLED);
            break;

        case OTA_YMODEM_EVENT_ERROR:
            boot_transport_write_byte(OTA_YMODEM_NAK);
            update->retry_count++;
            if (update->retry_count > BOOT_OTA_MAX_RETRY) {
                boot_update_fail(update, BOOT_UPDATE_REASON_RETRY_EXCEEDED);
            }
            break;

        default:
            break;
    }
}

/**
 * @brief  判断当前阶段是否已经静默过久
 * @param  update 会话对象
 * @param  now    当前毫秒计数
 * @return true 表示应当判定为超时
 */
static bool boot_update_is_silent(const boot_update_t *update, uint32_t now)
{
    uint32_t limit = (update->state == BOOT_UPDATE_STATE_HANDSHAKE) ?
                     BOOT_OTA_HANDSHAKE_TIMEOUT_MS : BOOT_OTA_SILENCE_TIMEOUT_MS;

    return (now - update->progress_tick) > limit;
}

void boot_update_init(boot_update_t *update, boot_metadata_t *metadata)
{
    if (update == NULL) {
        return;
    }

    (void)memset(update, 0, sizeof(*update));
    update->metadata = metadata;
    update->target_slot = BOOT_SLOT_ID_NONE;
    update->protected_slot = BOOT_SLOT_ID_NONE;
    update->state = BOOT_UPDATE_STATE_IDLE;
    update->reason = BOOT_UPDATE_REASON_NONE;
}

bool boot_update_start(boot_update_t *update,
                       uint32_t target_slot,
                       uint32_t protected_slot,
                       uint32_t version,
                       bool has_version)
{
    if (update == NULL) {
        return false;
    }
    if ((target_slot != BOOT_SLOT_ID_A) && (target_slot != BOOT_SLOT_ID_B)) {
        return false;
    }
    if (boot_update_is_busy(update)) {
        return false;
    }

    /* 只重置会话相关字段，不动 metadata 关联。 */
    ota_ymodem_rx_init(&update->rx);
    (void)memset(&update->writer, 0, sizeof(update->writer));
    update->target_slot = target_slot;
    update->protected_slot = protected_slot;
    update->version = version;
    update->has_version = has_version;
    update->expected_index = 0u;
    update->retry_count = 0u;
    update->image_size = 0u;
    update->image_crc32 = 0u;
    update->has_signature = false;
    update->sig_algorithm = OTA_IMAGE_SIG_NONE;
    update->pubkey_id = 0u;
    update->reason = BOOT_UPDATE_REASON_NONE;
    (void)memset(update->signature, 0, sizeof(update->signature));
    (void)memset(update->file_name, 0, sizeof(update->file_name));

    boot_update_enter(update, BOOT_UPDATE_STATE_HANDSHAKE);

    /* 首个 'C' 不在这里发，交给第一次 poll：命令应答必须先上线，否则 'C'
       会插在应答行前面，上位机读到的第一行就成了 "COK ota=recv ..."。把计时
       基点往前拨一个周期，第一次 poll 就会立刻把 'C' 发出去。 */
    update->c_tick = update->state_tick - BOOT_OTA_C_INTERVAL_MS;

    return true;
}

void boot_update_poll(boot_update_t *update)
{
    uint8_t byte;
    ota_ymodem_event_t event;
    uint32_t now;

    if (!boot_update_is_busy(update)) {
        return;
    }

    while (boot_transport_read(&byte)) {
        now = HAL_GetTick();
        update->progress_tick = now;

        /* 握手期按 Ctrl-C 取消：这个阶段收到的字节不可能属于载荷，可以安全拦截。 */
        if ((update->state == BOOT_UPDATE_STATE_HANDSHAKE) && (byte == BOOT_UPDATE_KEY_CANCEL)) {
            boot_update_fail(update, BOOT_UPDATE_REASON_CANCELLED);
            return;
        }

        event = ota_ymodem_rx_feed(&update->rx, byte);
        boot_update_handle_event(update, event);

        if (!boot_update_is_busy(update)) {
            return;
        }
    }

    now = HAL_GetTick();

    /* 静默超时：对端彻底没动静了。 */
    if (boot_update_is_silent(update, now)) {
        boot_update_fail(update, BOOT_UPDATE_REASON_TIMEOUT);
        return;
    }

    /* 停滞超时：字节一直有，但迟迟凑不成一帧，多半是波特率或接线不对。 */
    if ((now - update->state_tick) > BOOT_OTA_STALL_TIMEOUT_MS) {
        boot_update_fail(update, BOOT_UPDATE_REASON_STALLED);
        return;
    }

    /* 握手期周期发 'C'：对端可能是在我们发 'C' 之前启动的。 */
    if ((update->state == BOOT_UPDATE_STATE_HANDSHAKE) &&
        ((now - update->c_tick) >= BOOT_OTA_C_INTERVAL_MS)) {
        boot_transport_write_byte(OTA_YMODEM_C);
        update->c_tick = now;
    }
}

void boot_update_abort(boot_update_t *update)
{
    if (update == NULL) {
        return;
    }

    boot_update_fail(update, BOOT_UPDATE_REASON_CANCELLED);
}

bool boot_update_is_busy(const boot_update_t *update)
{
    if (update == NULL) {
        return false;
    }

    return (update->state == BOOT_UPDATE_STATE_HANDSHAKE) ||
           (update->state == BOOT_UPDATE_STATE_RECEIVE) ||
           (update->state == BOOT_UPDATE_STATE_FINAL);
}

boot_update_state_t boot_update_state(const boot_update_t *update)
{
    if (update == NULL) {
        return BOOT_UPDATE_STATE_IDLE;
    }

    return update->state;
}

boot_update_reason_t boot_update_reason(const boot_update_t *update)
{
    if (update == NULL) {
        return BOOT_UPDATE_REASON_NONE;
    }

    return update->reason;
}

const char *boot_update_state_name(boot_update_state_t state)
{
    switch (state) {
        case BOOT_UPDATE_STATE_IDLE:
            return "IDLE";
        case BOOT_UPDATE_STATE_HANDSHAKE:
            return "HANDSHAKE";
        case BOOT_UPDATE_STATE_RECEIVE:
            return "RECEIVE";
        case BOOT_UPDATE_STATE_FINAL:
            return "FINAL";
        case BOOT_UPDATE_STATE_DONE:
            return "DONE";
        case BOOT_UPDATE_STATE_FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

const char *boot_update_reason_name(boot_update_reason_t reason)
{
    switch (reason) {
        case BOOT_UPDATE_REASON_NONE:
            return "NONE";
        case BOOT_UPDATE_REASON_TIMEOUT:
            return "TIMEOUT";
        case BOOT_UPDATE_REASON_STALLED:
            return "STALLED";
        case BOOT_UPDATE_REASON_CANCELLED:
            return "CANCELLED";
        case BOOT_UPDATE_REASON_RETRY_EXCEEDED:
            return "RETRY_EXCEEDED";
        case BOOT_UPDATE_REASON_NO_VERSION:
            return "NO_VERSION";
        case BOOT_UPDATE_REASON_BAD_SLOT:
            return "BAD_SLOT";
        case BOOT_UPDATE_REASON_BAD_SIZE:
            return "BAD_SIZE";
        case BOOT_UPDATE_REASON_ERASE_FAILED:
            return "ERASE_FAILED";
        case BOOT_UPDATE_REASON_WRITE_FAILED:
            return "WRITE_FAILED";
        case BOOT_UPDATE_REASON_VERIFY_FAILED:
            return "VERIFY_FAILED";
        case BOOT_UPDATE_REASON_SIGNATURE_FAILED:
            return "SIGNATURE_FAILED";
        case BOOT_UPDATE_REASON_COMMIT_FAILED:
            return "COMMIT_FAILED";
        default:
            return "UNKNOWN";
    }
}
