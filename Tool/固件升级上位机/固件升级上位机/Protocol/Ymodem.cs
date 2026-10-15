using System.Globalization;

namespace 固件升级上位机;

/// <summary>
/// YMODEM 的线上控制字与组帧规则。
/// </summary>
/// <remarks>
/// 设备永远是被动方：握手由设备发 'C' 发起，上位机只负责应答与发包。
/// 帧布局为 <c>帧首 + 序号 + 序号反码 + 数据体 + CRC16（高字节先）</c>，
/// 其中序号与反码之和恒为 0xFF。
/// </remarks>
internal static class Ymodem
{
    /// <summary>128 字节短包的帧首。</summary>
    public const byte Soh = 0x01;

    /// <summary>1024 字节长包的帧首。</summary>
    public const byte Stx = 0x02;

    /// <summary>传输结束符。</summary>
    public const byte Eot = 0x04;

    /// <summary>接收正确。</summary>
    public const byte Ack = 0x06;

    /// <summary>校验出错，请求重传。</summary>
    public const byte Nak = 0x15;

    /// <summary>主动中止，接收方连续两个才算数。</summary>
    public const byte Can = 0x18;

    /// <summary>请求以 CRC16 模式开始传输。</summary>
    public const byte C = 0x43;

    /// <summary>短包数据体长度。</summary>
    public const int ShortPacketSize = 128;

    /// <summary>长包数据体长度。</summary>
    public const int LongPacketSize = 1024;

    /// <summary>末包补齐用的填充字节，接收方按声明长度截断、不会写进槽里。</summary>
    public const byte PadByte = 0x1A;

    /// <summary>文件名字段的最大字节数，含结尾 '\0'。</summary>
    public const int FileNameLimit = 64;

    /// <summary>按剩余字节数选取本次包的数据体长度。</summary>
    /// <param name="remaining">尚未发送的镜像字节数。</param>
    /// <returns>128 或 1024。</returns>
    public static int SelectPayloadSize(int remaining)
    {
        // 临时固定使用 128 字节短包，绕过 1K 长包链路以便定位串口丢帧问题。
        // 即使剩余数据超过 128 字节，也由发送端拆成多个 SOH 帧发送。
        return ShortPacketSize;
    }

    /// <summary>组装第 0 包（文件信息包）。</summary>
    /// <param name="fileName">镜像文件名，仅用于设备侧日志。</param>
    /// <param name="fileSize">镜像字节数，必须与真实文件长度一致。</param>
    /// <param name="version">版本号，null 表示不声明。</param>
    /// <param name="signature">64 字节 Ed25519 签名，null 表示不签名。</param>
    /// <param name="pubkeyId">签名所用公钥编号，写入 <c>kid=</c> 字段。</param>
    /// <returns>可直接写出的完整帧。</returns>
    /// <remarks>
    /// 第二字段形如 "<c>&lt;长度&gt; [0x版本] [sig=&lt;128 hex&gt;] [kid=&lt;2 hex&gt;]</c>"。
    /// 128 字节的体装不下 filename + size + version + 132 字符的 sig 令牌，
    /// 因此本包固定用 1024 字节长包（帧首 STX）承载。结束包不受影响，仍走 128 字节短包。
    /// </remarks>
    public static byte[] BuildFileInfoPacket(string fileName, int fileSize, uint? version,
                                             byte[]? signature, byte pubkeyId)
    {
        byte[] body = new byte[LongPacketSize];
        int position = 0;

        foreach (char ch in TrimFileName(fileName))
        {
            body[position++] = (byte)ch;
        }

        body[position++] = 0x00;

        // 第二字段形如 "<长度> 0x<版本>"。版本号必须带 0x 前缀，
        // 否则设备会把它当成标准 YMODEM 的 mtime 而忽略。
        string field = fileSize.ToString(CultureInfo.InvariantCulture);
        if (version.HasValue)
        {
            field += " 0x" + version.Value.ToString("X8", CultureInfo.InvariantCulture);
        }

        /* sig= 必须是恰好 128 个十六进制字符；kid= 只有随签名一起出现才有意义，
           未签名时不得带任何 sig=/kid= 令牌。 */
        if (signature is { Length: FirmwareSigner.SignatureLength })
        {
            field += " sig=" + Convert.ToHexString(signature);
            field += " kid=" + pubkeyId.ToString("X2", CultureInfo.InvariantCulture);
        }

        foreach (char ch in field)
        {
            body[position++] = (byte)ch;
        }

        body[position] = 0x00;

        return BuildFrame(0, body, LongPacketSize);
    }

    /// <summary>组装结束包。</summary>
    /// <remarks>
    /// 设备侧 <c>ota_ymodem_parse_file_info()</c> 要求第二字段非空，因此长度字段
    /// 必须写成 "0" 而不是纯 0——全零的结束包会被判成格式非法并被拒绝。
    /// </remarks>
    /// <returns>可直接写出的完整帧。</returns>
    public static byte[] BuildEndPacket()
    {
        byte[] body = new byte[ShortPacketSize];

        body[0] = 0x00;         // 空文件名
        body[1] = (byte)'0';    // 长度为 "0"
        body[2] = 0x00;

        return BuildFrame(0, body, ShortPacketSize);
    }

    /// <summary>组装一个数据包。</summary>
    /// <param name="index">8 位包序号。</param>
    /// <param name="data">源数据，必须至少包含 <paramref name="payloadSize"/> 字节。</param>
    /// <param name="offset">本包在源数据中的起始偏移。</param>
    /// <param name="payloadSize">本包数据体长度，128 或 1024。</param>
    /// <returns>可直接写出的完整帧。</returns>
    public static byte[] BuildDataPacket(byte index, ReadOnlySpan<byte> data, int offset, int payloadSize)
    {
        byte[] body = new byte[payloadSize];
        int available = Math.Min(payloadSize, data.Length - offset);

        data.Slice(offset, available).CopyTo(body);
        for (int i = available; i < payloadSize; i++)
        {
            body[i] = PadByte;
        }

        return BuildFrame(index, body, payloadSize);
    }

    /// <summary>按帧格式补齐序号、反码与 CRC16。</summary>
    private static byte[] BuildFrame(byte index, byte[] body, int payloadSize)
    {
        byte[] frame = new byte[3 + payloadSize + 2];

        frame[0] = (payloadSize == LongPacketSize) ? Stx : Soh;
        frame[1] = index;
        frame[2] = (byte)(0xFF - index);
        Array.Copy(body, 0, frame, 3, payloadSize);

        ushort crc = Crc16.Compute(frame.AsSpan(3, payloadSize));
        frame[3 + payloadSize] = (byte)(crc >> 8);
        frame[4 + payloadSize] = (byte)(crc & 0xFF);

        return frame;
    }

    /// <summary>截断过长的文件名，保证第 0 包的两个字段都装得下。</summary>
    /// <remarks>
    /// 文件名限 63 字符 + 结尾 '\0'；第二字段最坏情形约为
    /// <c>size + " 0x" + 8 位版本 + " sig=" + 128 + " kid=" + 2</c>（约 160 字符），
    /// 加上文件名仍远小于 1024 字节的体长，因此不会溢出。
    /// </remarks>
    private static string TrimFileName(string fileName)
    {
        string name = Path.GetFileName(fileName);

        if (string.IsNullOrEmpty(name))
        {
            name = "firmware.bin";
        }

        // 留出文件名结尾 '\0'、长度字段与版本字段的空间。
        return (name.Length > FileNameLimit - 1) ? name[..(FileNameLimit - 1)] : name;
    }
}
