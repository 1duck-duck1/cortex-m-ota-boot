/**
 ******************************************************************************
 * @file    ota_cli.c
 * @brief   行式命令行框架实现
 ******************************************************************************
 */
#include "ota_cli.h"

#include <string.h>

#define OTA_CLI_HEX_PREFIX_LENGTH   (2u)    /* "0x" 的长度 */
#define OTA_CLI_VERSION_MAX_DIGITS  (8u)    /* u32 最多 8 位十六进制 */
#define OTA_CLI_REPLY_RESERVED      (3u)    /* 为 "\r\n" 与结尾 '\0' 预留 */

static const char s_hex_digits[16] = "0123456789ABCDEF";

/**
 * @brief  取错误码的可读名称
 * @param  code 错误码
 * @return 静态字符串，永不返回 NULL
 */
static const char *ota_cli_result_name(ota_cli_result_t code)
{
    switch (code) {
        case OTA_CLI_ERR_UNKNOWN_COMMAND:
            return "UNKNOWN_COMMAND";
        case OTA_CLI_ERR_BAD_PARAM:
            return "BAD_PARAM";
        case OTA_CLI_ERR_FAILED:
            return "FAILED";
        default:
            return "ERROR";
    }
}

/**
 * @brief  通过写出函数输出一段文本
 * @param  cli  命令行实例
 * @param  text 待输出文本
 * @param  size 字节数
 */
static void ota_cli_write_text(ota_cli_t *cli, const char *text, uint32_t size)
{
    if (cli->write != NULL) {
        cli->write(text, size, cli->write_context);
    }
}

/**
 * @brief  向应答行尾部追加文本，超出缓冲即截断
 * @param  cli  命令行实例
 * @param  text 待追加文本
 * @param  size 字节数
 */
static void ota_cli_reply_append(ota_cli_t *cli, const char *text, uint32_t size)
{
    uint32_t capacity = (uint32_t)(OTA_CLI_REPLY_MAX - OTA_CLI_REPLY_RESERVED);
    uint32_t available;

    if (cli->reply_length >= capacity) {
        return;
    }

    available = capacity - cli->reply_length;
    if (size > available) {
        size = available;
    }

    (void)memcpy(&cli->reply[cli->reply_length], text, size);
    cli->reply_length += size;
}

/**
 * @brief  追加 " key=" 前缀
 * @param  cli 命令行实例
 * @param  key 键名
 */
static void ota_cli_reply_append_key(ota_cli_t *cli, const char *key)
{
    ota_cli_reply_append(cli, " ", 1u);
    ota_cli_reply_append(cli, key, (uint32_t)strlen(key));
    ota_cli_reply_append(cli, "=", 1u);
}

/**
 * @brief  逐个输出命令表的用法，每行一条
 * @param  cli 命令行实例
 */
static void ota_cli_show_help(ota_cli_t *cli)
{
    uint32_t index;

    for (index = 0u; index < cli->command_count; index++) {
        ota_cli_reply_ok_begin(cli);
        ota_cli_reply_kv_str(cli, "cmd", cli->commands[index].name);
        ota_cli_reply_kv_str(cli, "usage", cli->commands[index].usage);
        ota_cli_reply_end(cli);
    }
}

/**
 * @brief  把当前行切成参数
 * @param  cli 命令行实例
 * @note   就地切分：分隔符被替换成 '\0'，argv 指向 line 内部。
 */
static void ota_cli_split(ota_cli_t *cli)
{
    uint32_t index = 0u;

    cli->argc = 0u;

    while (cli->line[index] != '\0') {
        while ((cli->line[index] == ' ') || (cli->line[index] == '\t')) {
            index++;
        }
        if (cli->line[index] == '\0') {
            break;
        }

        if (cli->argc < (uint32_t)OTA_CLI_ARG_MAX) {
            cli->argv[cli->argc] = &cli->line[index];
            cli->argc++;
        }

        while ((cli->line[index] != '\0') &&
               (cli->line[index] != ' ') && (cli->line[index] != '\t')) {
            index++;
        }
        if (cli->line[index] == '\0') {
            break;
        }

        cli->line[index] = '\0';
        index++;
    }
}

/**
 * @brief  执行当前行
 * @param  cli 命令行实例
 * @note   处理函数未自行应答且返回值非 OK 时，由框架统一回一行 ERR，
 *         避免"命令失败却什么都不回"这种上位机无法判断的状态。
 */
static void ota_cli_execute(ota_cli_t *cli)
{
    const ota_cli_command_t *command = NULL;
    ota_cli_result_t result;
    uint32_t index;

    cli->line[cli->line_length] = '\0';
    cli->line_length = 0u;
    cli->is_reply_sent = false;
    cli->reply_length = 0u;

    ota_cli_split(cli);

    if (cli->argc == 0u) {
        return;
    }

    if (strcmp(cli->argv[0], "help") == 0) {
        ota_cli_show_help(cli);
        return;
    }

    for (index = 0u; index < cli->command_count; index++) {
        if (strcmp(cli->argv[0], cli->commands[index].name) == 0) {
            command = &cli->commands[index];
            break;
        }
    }

    if (command == NULL) {
        ota_cli_reply_err_begin(cli, OTA_CLI_ERR_UNKNOWN_COMMAND);
        ota_cli_reply_kv_str(cli, "reason", "UNKNOWN_COMMAND");
        ota_cli_reply_end(cli);
        return;
    }

    result = command->handler((int)cli->argc, cli->argv, cli->handler_context);
    if ((result != OTA_CLI_OK) && (!cli->is_reply_sent)) {
        ota_cli_reply_err_begin(cli, result);
        ota_cli_reply_kv_str(cli, "reason", ota_cli_result_name(result));
        ota_cli_reply_end(cli);
    }
}

void ota_cli_init(ota_cli_t *cli,
                  const ota_cli_command_t *commands,
                  uint32_t command_count,
                  void *handler_context,
                  ota_cli_write_fn_t write,
                  void *write_context)
{
    (void)memset(cli, 0, sizeof(ota_cli_t));

    cli->commands = commands;
    cli->command_count = command_count;
    cli->handler_context = handler_context;
    cli->write = write;
    cli->write_context = write_context;
}

void ota_cli_feed(ota_cli_t *cli, const uint8_t *data, uint32_t size)
{
    uint32_t index;
    uint8_t byte;

    if ((cli == NULL) || (data == NULL)) {
        return;
    }

    for (index = 0u; index < size; index++) {
        byte = data[index];

        if (byte == (uint8_t)'\r') {
            ota_cli_write_text(cli, "\r\n", 2u);
            if (cli->line_length > 0u) {
                ota_cli_execute(cli);
            }
            continue;
        }
        if (byte == (uint8_t)'\n') {
            /* 终端只发 '\n' 时补一次执行；发 "\r\n" 的终端在这里被去重。 */
            if (cli->line_length > 0u) {
                ota_cli_write_text(cli, "\r\n", 2u);
                ota_cli_execute(cli);
            }
            continue;
        }
        if ((byte == (uint8_t)0x08u) || (byte == (uint8_t)0x7Fu)) {
            if (cli->line_length > 0u) {
                cli->line_length--;
                ota_cli_write_text(cli, "\b \b", 3u);
            }
            continue;
        }
        if (byte == (uint8_t)0x03u) {
            cli->line_length = 0u;
            ota_cli_write_text(cli, "^C\r\n", 4u);
            continue;
        }
        if ((byte >= (uint8_t)0x20u) && (byte <= (uint8_t)0x7Eu)) {
            if (cli->line_length < (uint32_t)(OTA_CLI_LINE_MAX - 1u)) {
                cli->line[cli->line_length] = (char)byte;
                cli->line_length++;
                ota_cli_write_text(cli, (const char *)&byte, 1u);
            }
            continue;
        }
    }
}

bool ota_cli_parse_version(const char *text, uint32_t *version)
{
    uint32_t value = 0u;
    uint32_t digits = 0u;
    uint32_t index = OTA_CLI_HEX_PREFIX_LENGTH;
    uint32_t digit;
    char current;

    if ((text == NULL) || (version == NULL)) {
        return false;
    }
    if ((text[0] != '0') || ((text[1] != 'x') && (text[1] != 'X'))) {
        return false;
    }

    while (text[index] != '\0') {
        current = text[index];

        if ((current >= '0') && (current <= '9')) {
            digit = (uint32_t)(current - '0');
        } else if ((current >= 'a') && (current <= 'f')) {
            digit = (uint32_t)(current - 'a') + 10u;
        } else if ((current >= 'A') && (current <= 'F')) {
            digit = (uint32_t)(current - 'A') + 10u;
        } else {
            return false;
        }

        if (digits >= OTA_CLI_VERSION_MAX_DIGITS) {
            return false;
        }

        value = (value << 4u) | digit;
        digits++;
        index++;
    }

    if (digits == 0u) {
        return false;
    }

    *version = value;

    return true;
}

void ota_cli_reply_ok_begin(ota_cli_t *cli)
{
    cli->reply_length = 0u;
    ota_cli_reply_append(cli, "OK", 2u);
}

void ota_cli_reply_err_begin(ota_cli_t *cli, ota_cli_result_t code)
{
    char text[2];
    uint32_t value = (uint32_t)code;

    if (value > 9u) {
        value = 9u;
    }

    text[0] = (char)('0' + (char)value);
    text[1] = '\0';

    cli->reply_length = 0u;
    ota_cli_reply_append(cli, "ERR ", 4u);
    ota_cli_reply_append(cli, text, 1u);
}

void ota_cli_reply_kv_str(ota_cli_t *cli, const char *key, const char *value)
{
    ota_cli_reply_append_key(cli, key);
    ota_cli_reply_append(cli, value, (uint32_t)strlen(value));
}

void ota_cli_reply_kv_u32(ota_cli_t *cli, const char *key, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;
    uint32_t remainder;

    ota_cli_reply_append_key(cli, key);

    if (value == 0u) {
        ota_cli_reply_append(cli, "0", 1u);
        return;
    }

    while ((value > 0u) && (count < (uint32_t)sizeof(digits))) {
        remainder = value % 10u;
        digits[count] = (char)('0' + (char)remainder);
        value /= 10u;
        count++;
    }

    while (count > 0u) {
        count--;
        ota_cli_reply_append(cli, &digits[count], 1u);
    }
}

void ota_cli_reply_kv_hex(ota_cli_t *cli, const char *key, uint32_t value)
{
    char digits[10];
    uint32_t index;

    ota_cli_reply_append_key(cli, key);
    ota_cli_reply_append(cli, "0x", 2u);

    for (index = 0u; index < 8u; index++) {
        digits[index] = s_hex_digits[(value >> (28u - (index * 4u))) & 0x0Fu];
    }
    ota_cli_reply_append(cli, digits, 8u);
}

void ota_cli_reply_end(ota_cli_t *cli)
{
    ota_cli_reply_append(cli, "\r\n", 2u);
    cli->reply[cli->reply_length] = '\0';

    ota_cli_write_text(cli, cli->reply, cli->reply_length);

    cli->is_reply_sent = true;
    cli->reply_length = 0u;
}
