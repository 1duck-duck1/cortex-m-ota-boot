using System.Globalization;
using Org.BouncyCastle.Crypto.Parameters;

namespace 固件升级上位机;

/// <summary>
/// Ed25519 密钥文件读取：把文件内容解析成 32 字节私钥种子。
/// </summary>
/// <remarks>
/// 文件是纯文本，内容为 64 个十六进制字符（可带空格与换行）或 32 字节的 base64。
/// 缺失、长度不对或格式非法都抛 <see cref="OtaUploadException"/>，让调用方把原因直接展示给用户。
/// </remarks>
internal static class Ed25519KeyFile
{
    /// <summary>Ed25519 私钥种子长度，单位字节。</summary>
    public const int SeedLength = 32;

    /// <summary>十六进制文本形式的种子字符数。</summary>
    public const int SeedHexLength = SeedLength * 2;

    /// <summary>读取密钥文件并构造 Ed25519 私钥参数。</summary>
    /// <param name="path">密钥文件路径。</param>
    /// <returns>由文件里 32 字节种子构造的私钥参数。</returns>
    /// <exception cref="OtaUploadException">路径为空、文件不存在、读取失败或内容格式非法。</exception>
    public static Ed25519PrivateKeyParameters Load(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new OtaUploadException("签名密钥路径为空。");
        }

        if (!File.Exists(path))
        {
            throw new OtaUploadException($"签名密钥文件不存在：{path}");
        }

        string text;

        try
        {
            text = File.ReadAllText(path);
        }
        catch (IOException exception)
        {
            throw new OtaUploadException($"读取签名密钥失败：{exception.Message}");
        }

        return new Ed25519PrivateKeyParameters(ParseSeed(text, path), 0);
    }

    /// <summary>把文本内容解析成 32 字节种子。</summary>
    /// <param name="text">文件全文。</param>
    /// <param name="path">文件路径，仅用于报错。</param>
    /// <returns>32 字节种子。</returns>
    /// <exception cref="OtaUploadException">内容为空或既不是合法 hex 也不是合法 base64。</exception>
    private static byte[] ParseSeed(string text, string path)
    {
        string compact = RemoveWhitespace(text);

        if (compact.Length == 0)
        {
            throw new OtaUploadException($"签名密钥文件为空：{path}");
        }

        // 优先按 hex 解析：恰好 64 个十六进制字符才算数。
        if ((compact.Length == SeedHexLength) && TryParseHex(compact, out byte[] hex))
        {
            return hex;
        }

        try
        {
            byte[] base64 = Convert.FromBase64String(compact);

            if (base64.Length == SeedLength)
            {
                return base64;
            }
        }
        catch (FormatException)
        {
            // 落到下面的统一报错。
        }

        throw new OtaUploadException(
            $"签名密钥格式非法：{path}（须为 64 个十六进制字符或 32 字节 base64）。");
    }

    /// <summary>剔除空白字符，兼容带换行或分组的文本。</summary>
    private static string RemoveWhitespace(string text)
    {
        return string.Concat(text.Where(ch => !char.IsWhiteSpace(ch)));
    }

    /// <summary>把偶数长度的十六进制文本解析成字节数组。</summary>
    /// <param name="text">已剔除空白的文本。</param>
    /// <param name="bytes">解析结果，失败时为空数组。</param>
    /// <returns>true 表示全部为合法十六进制字符。</returns>
    private static bool TryParseHex(string text, out byte[] bytes)
    {
        bytes = [];

        if ((text.Length % 2) != 0)
        {
            return false;
        }

        byte[] result = new byte[text.Length / 2];

        for (int i = 0; i < result.Length; i++)
        {
            if (!byte.TryParse(text.AsSpan(i * 2, 2), NumberStyles.HexNumber,
                               CultureInfo.InvariantCulture, out result[i]))
            {
                return false;
            }
        }

        bytes = result;
        return true;
    }
}
