/**
 ******************************************************************************
 * @file    ota_cli.h
 * @brief   行式命令行框架：行编辑、命令表分发、单行应答组装
 *
 * 命令行是升级的唯一入口：Boot 与 App 都只用它接收指令，YMODEM 会话由 'ota recv'
 * 子命令登记后启动。框架本身不含任何业务命令，命令表由各工程注册，因此两侧的
 * 交互风格（应答格式、错误码）天然一致。
 *
 * 应答是"先攒后发"的单行文本：reply_ok_begin / reply_err_begin 开一行，
 * 若干 reply_kv_* 追加键值，reply_end 一次性写出并以 CRLF 收尾。
 ******************************************************************************
 */
#ifndef OTA_CLI_H
#define OTA_CLI_H

#include <stdbool.h>
#include <stdint.h>

/** 单行命令的最大长度，含结尾的 '\0' */
#define OTA_CLI_LINE_MAX      (96u)

/** 单行命令最多切出的参数个数，含命令词本身 */
#define OTA_CLI_ARG_MAX       (8u)

/** 单行应答缓冲长度，超长部分被截断但仍保证以 CRLF 收尾 */
#define OTA_CLI_REPLY_MAX     (192u)

/** 命令处理结果，同时作为 ERR 应答里的错误码 */
typedef enum
{
    OTA_CLI_OK = 0,                 /**< 处理成功 */
    OTA_CLI_ERR_UNKNOWN_COMMAND = 1,/**< 命令词未被注册 */
    OTA_CLI_ERR_BAD_PARAM = 2,      /**< 参数个数或取值非法，设备不接受本次声明 */
    OTA_CLI_ERR_FAILED = 3          /**< 参数合法但执行失败，多为 Flash 或链路异常 */
} ota_cli_result_t;

/**
 * @brief  命令处理函数原型
 * @param  argc             参数个数，含命令词本身
 * @param  argv             参数数组，指向框架内部的可写行缓冲
 * @param  handler_context  注册命令表时传入的上下文
 * @return OTA_CLI_OK 表示已自行应答或无需应答；其他值在未自行应答时由框架统一回 ERR
 */
typedef ota_cli_result_t (*ota_cli_handler_t)(int argc, char *argv[], void *handler_context);

/**
 * @brief  应答写出函数原型
 * @param  text          待写出的字节，不是以 '\0' 结尾的字符串
 * @param  size          字节数
 * @param  write_context 初始化时传入的上下文
 * @note   必须是同步写出：命令处理函数返回后，框架认为该行已在线上。
 */
typedef void (*ota_cli_write_fn_t)(const char *text, uint32_t size, void *write_context);

/** 一条注册进命令表的命令 */
typedef struct
{
    const char *name;           /**< 命令词，全小写 */
    const char *usage;          /**< 用法串，供 help 展示 */
    ota_cli_handler_t handler;  /**< 处理函数 */
} ota_cli_command_t;

/** 命令行实例，由调用方持有；含行缓冲与应答缓冲，体积较大，不要放栈上 */
typedef struct
{
    const ota_cli_command_t *commands;  /**< 命令表 */
    uint32_t command_count;             /**< 命令表项数 */
    void *handler_context;              /**< 透传给处理函数的上下文 */
    ota_cli_write_fn_t write;           /**< 应答与回显的写出函数 */
    void *write_context;                /**< 透传给写出函数的上下文 */
    char line[OTA_CLI_LINE_MAX];        /**< 当前行的输入缓冲 */
    uint32_t line_length;               /**< 已录入字节数 */
    char reply[OTA_CLI_REPLY_MAX];      /**< 正在组装的应答行 */
    uint32_t reply_length;              /**< 已组装字节数 */
    bool is_reply_sent;                 /**< 本次命令是否已经应答过 */
    char *argv[OTA_CLI_ARG_MAX];        /**< 切词结果，指向 line 内部 */
    uint32_t argc;                      /**< 切出的参数个数 */
} ota_cli_t;

/**
 * @brief  初始化命令行实例
 * @param  cli            待初始化的实例
 * @param  commands       命令表
 * @param  command_count  命令表项数
 * @param  handler_context 透传给处理函数的上下文，可为 NULL
 * @param  write          应答与回显的写出函数，不允许为 NULL
 * @param  write_context  透传给写出函数的上下文，可为 NULL
 * @note   只记录指针不复制命令表，命令表必须是静态存储期。
 */
void ota_cli_init(ota_cli_t *cli,
                  const ota_cli_command_t *commands,
                  uint32_t command_count,
                  void *handler_context,
                  ota_cli_write_fn_t write,
                  void *write_context);

/**
 * @brief  喂入一段收到的字节，按行执行命令
 * @param  cli  命令行实例
 * @param  data 收到的字节
 * @param  size 字节数
 * @note   带回显：可打印字符原样回写，退格回显 "\b \b"，回车回显 CRLF。
 *         未录入完整行时不会执行任何命令，可以安全地按任意长度分片喂入。
 */
void ota_cli_feed(ota_cli_t *cli, const uint8_t *data, uint32_t size);

/**
 * @brief  解析版本号文本
 * @param  text    待解析文本
 * @param  version 解析结果
 * @return true 表示格式合法
 * @retval false 缺少 0x 前缀、含非十六进制字符，或超过 8 位
 * @note   强制要求 0x 前缀，是为了与 YMODEM 第 0 包里的标准 mtime 明确区分开：
 *         无前缀的纯数字一律按时间戳处理，不当作版本号。
 */
bool ota_cli_parse_version(const char *text, uint32_t *version);

/**
 * @brief  开始一行 OK 应答
 * @param  cli 命令行实例
 */
void ota_cli_reply_ok_begin(ota_cli_t *cli);

/**
 * @brief  开始一行 ERR 应答
 * @param  cli  命令行实例
 * @param  code 错误码，取自 ota_cli_result_t 的取值
 */
void ota_cli_reply_err_begin(ota_cli_t *cli, ota_cli_result_t code);

/**
 * @brief  追加一个字符串键值对
 * @param  cli   命令行实例
 * @param  key   键名
 * @param  value 值，不得含空格
 */
void ota_cli_reply_kv_str(ota_cli_t *cli, const char *key, const char *value);

/**
 * @brief  追加一个十进制键值对
 * @param  cli   命令行实例
 * @param  key   键名
 * @param  value 值
 */
void ota_cli_reply_kv_u32(ota_cli_t *cli, const char *key, uint32_t value);

/**
 * @brief  追加一个 0x 前缀的 8 位十六进制键值对
 * @param  cli   命令行实例
 * @param  key   键名
 * @param  value 值
 * @note   版本号、CRC32 这类"需要与工具输出逐位对照"的字段用它。
 */
void ota_cli_reply_kv_hex(ota_cli_t *cli, const char *key, uint32_t value);

/**
 * @brief  结束当前应答行并同步写出
 * @param  cli 命令行实例
 * @note   可以连续调用：每次调用都独立成行，help 就是靠这一点逐行输出命令表。
 */
void ota_cli_reply_end(ota_cli_t *cli);

#endif /* OTA_CLI_H */
