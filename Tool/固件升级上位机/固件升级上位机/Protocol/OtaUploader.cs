using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;

namespace 固件升级上位机;

/// <summary>一次升级会话的输入。</summary>
internal sealed class UploadRequest
{
    /// <summary>镜像字节，必须与设备要写入槽内的内容逐字节一致。</summary>
    public required byte[] Image { get; init; }

    /// <summary>镜像文件名，只用于第 0 包声明。</summary>
    public required string FileName { get; init; }

    /// <summary>版本号，null 表示不在第 0 包里声明。</summary>
    public uint? Version { get; init; }
}

/// <summary>一次升级会话的结果。</summary>
internal sealed class UploadResult
{
    /// <summary>会话是否成功。</summary>
    public required bool Success { get; init; }

    /// <summary>结果摘要，成功时来自设备的结论行，失败时是失败原因。</summary>
    public required string Summary { get; init; }
}

/// <summary>升级会话失败，携带可直接展示给用户的原因。</summary>
internal sealed class OtaUploadException : Exception
{
    /// <summary>构造失败异常。</summary>
    /// <param name="message">失败原因。</param>
    public OtaUploadException(string message) : base(message)
    {
    }
}

/// <summary>
/// YMODEM 发送侧：把一份镜像推给设备并核对设备回报的长度与 CRC32。
/// </summary>
/// <remarks>
/// 设备永远被动，因此本类的节奏完全跟随设备的应答：
/// <list type="number">
///   <item>下发 <c>ota recv 0xVER</c> 登记请求；</item>
///   <item>等设备发 <c>'C'</c> 发起 CRC16 模式握手；</item>
///   <item>发第 0 包（文件名 + 长度 + 版本），等 ACK 与随后的 <c>'C'</c>；
///         这段等待涵盖设备擦除整个非活动槽的耗时；</item>
///   <item>逐包发送 1024 / 128 字节数据包，收到 NAK 或超时就重传同一包；</item>
///   <item>发 EOT；Boot 侧收到即收尾，App 侧会再发一个 <c>'C'</c> 索要结束包。</item>
/// </list>
/// 结束包只在探测到第二个 <c>'C'</c> 时才发：Boot 侧收完 EOT 直接 finalize，
/// 多发一个包会被当成命令行噪声。
/// </remarks>
internal sealed class OtaUploader
{
    /* 这些上限只在失败时决定用户要空等多久，正常路径一收到应答就返回。
       取值按"正常应答时间 ×2~×20"来定：设备掉电时不会有任何回复，
       全靠它们把空等卡住。 */

    /// <summary>等设备发起 YMODEM 握手 'C' 的上限，单位毫秒。</summary>
    /// <remarks>设备就停在命令行上，登记完成后立刻回 'C'，应答在毫秒级。</remarks>
    private const int HandshakeTimeoutMs = 5000;

    /// <summary>等第 0 包被确认的上限，单位毫秒。</summary>
    /// <remarks>
    /// 这段等待里设备要擦掉目标槽的 3 个 128 KB 扇区（数据手册典型 1 s、最坏 4 s
    /// 每个，合计最坏约 12 s），所以不能按普通包的标准压低：压得太紧会在擦除尚未
    /// 结束时重传第 0 包，而那些字节正好落在擦除期间被丢掉，反而把会话搞死。
    /// </remarks>
    private const int EraseTimeoutMs = 20000;

    /// <summary>等单个数据包应答的上限，单位毫秒。</summary>
    /// <remarks>一帧 1029 字节在 115200 下上线约 90 ms，2 s 有约 20 倍余量。</remarks>
    private const int PacketTimeoutMs = 2000;

    /// <summary>等 EOT 应答与紧随其后的 'C' 的上限，单位毫秒。</summary>
    /// <remarks>设备收完 EOT 直接回 ACK，随后的回读复核也只是几十毫秒的 Flash 读。</remarks>
    private const int FinalAckTimeoutMs = 5000;

    private const int EndPacketProbeMs = 1500;
    private const int EndPacketAckTimeoutMs = 1500;
    private const int ReportWaitMs = 4000;

    /// <summary>同一个包连续重传的次数上限。</summary>
    /// <remarks>
    /// 有线串口上只有偶发噪声才会需要重传，3 次足够。原来的 10 次对掉电这类
    /// 真故障毫无帮助——对端已经没了，多等几轮只是把失败延后几分钟才报出来。
    /// </remarks>
    private const int MaxRetries = 3;

    /// <summary>单帧写出耗时超过此值就告警，单位毫秒。</summary>
    /// <remarks>
    /// 一帧 1029 字节在 115200 下只需约 89 ms 上线。若写出调用本身耗时远超这个数，
    /// 说明字节还堵在适配器或驱动里没出去，设备会因为收不到整帧而静默超时——现象与
    /// "设备侧丢字节"极易混淆，必须把这一项单独量出来才能定位。
    /// </remarks>
    private const int SlowWriteThresholdMs = 200;

    private static readonly WireResponse[] HandshakeTargets = [WireResponse.Ready];
    private static readonly WireResponse[] AckTargets = [WireResponse.Ack];
    private static readonly WireResponse[] AckOrNakTargets = [WireResponse.Ack, WireResponse.Nak];

    private readonly SerialLink _link;
    private readonly DeviceLineReader _lines;
    private readonly Action<string> _log;
    private readonly Action<UploadProgress> _progress;

    private string _lastReply = string.Empty;
    private string _deviceError = string.Empty;

    /// <summary>设备登记升级请求时回报的目标槽（"A"/"B"）；App 侧不回这一行，保持为空。</summary>
    private string _targetSlot = string.Empty;

    /// <summary>构造一次升级会话。</summary>
    /// <param name="link">已打开的串口链路。</param>
    /// <param name="lines">设备文本行解析器，用于抑制回显并取出应答行。</param>
    /// <param name="log">日志输出回调，由调用方负责切回 UI 线程。</param>
    /// <param name="progress">进度回调，由调用方负责切回 UI 线程。</param>
    public OtaUploader(SerialLink link,
                       DeviceLineReader lines,
                       Action<string> log,
                       Action<UploadProgress> progress)
    {
        _link = link;
        _lines = lines;
        _log = log;
        _progress = progress;
    }

    /// <summary>执行一次完整升级会话。</summary>
    /// <param name="request">会话输入。</param>
    /// <param name="cancellationToken">取消令牌。</param>
    /// <returns>会话结果。</returns>
    /// <exception cref="OtaUploadException">会话失败，异常消息即失败原因。</exception>
    /// <exception cref="OperationCanceledException">调用方取消。</exception>
    public UploadResult Upload(UploadRequest request, CancellationToken cancellationToken)
    {
        byte[] image = request.Image;

        if (image.Length == 0)
        {
            throw new OtaUploadException("镜像文件为空。");
        }

        uint localCrc32 = Crc32.Compute(image);

        _log($"[工具] 镜像 {request.FileName}：{image.Length} 字节，CRC32=0x{localCrc32:X8}");
        _log($"[工具] 镜像自检：{ImageProbe.Describe(image)}");

        try
        {
            _link.DiscardInput();
            _lastReply = string.Empty;
            _deviceError = string.Empty;
            _targetSlot = string.Empty;

            RegisterCommand(request.Version);

            if (WaitControl(HandshakeTargets, HandshakeTimeoutMs, cancellationToken) != WireResponse.Ready)
            {
                throw new OtaUploadException(HandshakeFailure("等待设备握手 'C'"));
            }

            /* 此时 _lastReply 里装的是 "ota recv" 的登记应答，它与本次传输的结论无关。
               先把里面的目标槽取出来做一次预检，再清掉，否则收尾阶段会误把登记应答
               当成设备结论：它不含 size / crc 字段，核对会空转通过，FINAL 阶段真正的
               失败反而被吞掉。 */
            CheckTargetSlot(image);
            _lastReply = string.Empty;
            _deviceError = string.Empty;

            _log("[工具] 设备已就绪，发送第 0 包（文件信息）");

            SendFileInfo(request, image.Length, cancellationToken);

            _log("[工具] 开始发送数据包");

            int sent = SendDataPackets(image, cancellationToken);

            _progress(new UploadProgress(sent, image.Length, "数据发送完毕，等待设备校验"));

            FinishSession(cancellationToken);

            VerifyReported(localCrc32, image.Length);

            LogSummary("成功", request.FileName, image.Length, localCrc32, _lastReply);

            return new UploadResult { Success = true, Summary = _lastReply };
        }
        catch (OtaUploadException exception)
        {
            LogSummary("失败", request.FileName, image.Length, localCrc32, exception.Message);
            throw;
        }
    }

    /// <summary>把设备登记应答里的目标槽与镜像所属槽比对，不一致就当场拒绝。</summary>
    /// <param name="image">待发送的裸镜像。</param>
    /// <remarks>
    /// 目标槽由设备推导（恒为非运行槽），上位机无法指定，因此"镜像链接在哪个槽"
    /// 必须与之相符。不提前挡住的话，错槽镜像要等全部传完、设备写完 Flash 才在
    /// 向量表预检里被拒（ERR 3 VERIFY_FAILED），既浪费一次整槽擦写，也让原因难以辨认。
    /// App 侧的 <c>ota recv</c> 只登记不回行，此时 _targetSlot 为空，跳过这次预检。
    /// </remarks>
    private void CheckTargetSlot(ReadOnlySpan<byte> image)
    {
        string? target = ReadTextField(_lastReply, "slot");

        if (target is null)
        {
            return;
        }

        _targetSlot = target;

        string? belongs = ImageProbe.DetectSlot(image);

        if ((belongs is not null) && !string.Equals(belongs, target, StringComparison.Ordinal))
        {
            throw new OtaUploadException(
                $"镜像与目标槽不匹配：这份镜像链接在 Slot {belongs}，设备的目标槽是 Slot {target}。" +
                $"设备只升级非运行槽，请改用 Slot {target} 的镜像。");
        }
    }

    /// <summary>输出一次会话的结论摘要，成功与失败共用。</summary>
    /// <param name="verdict">结论，如"成功"或"失败"。</param>
    /// <param name="fileName">镜像文件名。</param>
    /// <param name="imageSize">镜像字节数。</param>
    /// <param name="localCrc32">本地算出的载荷 CRC32。</param>
    /// <param name="detail">依据：成功时是设备结论行，失败时是失败原因。</param>
    private void LogSummary(string verdict, string fileName, int imageSize,
                            uint localCrc32, string detail)
    {
        _log($"[工具] ===== 会话结论：{verdict} =====");
        _log($"[工具] 文件：{fileName}（{imageSize} 字节，本地 CRC32=0x{localCrc32:X8}）");
        _log($"[工具] 目标槽：{((_targetSlot.Length == 0) ? "未知（App 侧不回报）" : _targetSlot)}");
        _log($"[工具] 依据：{detail}");
    }

    /// <summary>下发 <c>ota recv</c> 命令登记升级请求。</summary>
    private void RegisterCommand(uint? version)
    {
        string command = version.HasValue
            ? "ota recv 0x" + version.Value.ToString("X8", CultureInfo.InvariantCulture)
            : "ota recv";

        _log($"[工具] 下发命令：{command}");
        _lines.ExpectEcho(command);
        _link.WriteCommand(command);
    }

    /// <summary>发送第 0 包并等待设备擦完槽后回 ACK 与 'C'。</summary>
    /// <remarks>
    /// 设备是在擦除非活动槽**之后**才回 ACK 的（擦 3 个 128 KB 扇区需要可观的
    /// 时间），因此这一段的等待上限必须远大于普通包的应答超时。
    /// </remarks>
    private void SendFileInfo(UploadRequest request, int imageSize, CancellationToken cancellationToken)
    {
        byte[] packet = Ymodem.BuildFileInfoPacket(request.FileName, imageSize, request.Version);

        for (int attempt = 1; attempt <= MaxRetries; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            _link.Write(packet);

            WireResponse response = WaitControl(AckTargets, EraseTimeoutMs, cancellationToken);
            if (response == WireResponse.Ack)
            {
                break;
            }

            /* 设备已经明确拒绝或中止时重传没有意义，只会把真正的失败原因埋进重传日志。 */
            if (response == WireResponse.DeviceError)
            {
                throw new OtaUploadException(HandshakeFailure("第 0 包被拒绝"));
            }
            if (response == WireResponse.Can)
            {
                throw new OtaUploadException("设备在接收第 0 包时主动中止（CAN）。");
            }

            if (attempt == MaxRetries)
            {
                throw new OtaUploadException(HandshakeFailure("第 0 包未被确认"));
            }

            _log($"[工具] 第 0 包重传 {attempt}/{MaxRetries}（{Describe(response)}）");
        }

        _log("[工具] 设备已擦除目标槽，等待开始信号 'C'");

        if (WaitControl(HandshakeTargets, FinalAckTimeoutMs, cancellationToken) != WireResponse.Ready)
        {
            throw new OtaUploadException(HandshakeFailure("等待数据阶段 'C'"));
        }
    }

    /// <summary>逐包发送镜像数据，返回实际发送的字节数。</summary>
    private int SendDataPackets(ReadOnlySpan<byte> image, CancellationToken cancellationToken)
    {
        int offset = 0;
        byte index = 1;
        int retries = 0;

        while (offset < image.Length)
        {
            cancellationToken.ThrowIfCancellationRequested();

            int remaining = image.Length - offset;
            int payloadSize = Ymodem.SelectPayloadSize(remaining);
            byte[] frame = Ymodem.BuildDataPacket(index, image, offset, payloadSize);

            long writeMs = WriteFrame(frame, index);

            WireResponse response = WaitControl(AckOrNakTargets, PacketTimeoutMs, cancellationToken);
            if (response == WireResponse.Ack)
            {
                offset += Math.Min(payloadSize, remaining);
                index = (byte)((index + 1) & 0xFF);
                retries = 0;
                _progress(new UploadProgress(offset, image.Length, "正在写入"));
                continue;
            }

            /* 设备已经明确拒绝或中止时重传没有意义，立刻把原因抛给用户。 */
            if (response == WireResponse.DeviceError)
            {
                throw new OtaUploadException($"设备在接收第 {index} 包时中止：{_deviceError}");
            }
            if (response == WireResponse.Can)
            {
                throw new OtaUploadException($"设备在第 {index} 包处主动中止（CAN）。");
            }

            retries++;
            if (retries > MaxRetries)
            {
                throw new OtaUploadException(
                    $"第 {index} 包连续 {MaxRetries} 次未被确认（{Describe(response)}）。" +
                    HintFor(response));
            }

            _log($"[工具] 第 {index} 包重传 {retries}/{MaxRetries}（{Describe(response)}，上次写出 {writeMs} ms）");
        }

        return offset;
    }

    /// <summary>整帧写出，并把写出耗时量出来。</summary>
    /// <param name="frame">完整的 YMODEM 帧。</param>
    /// <param name="index">帧序号，仅用于告警文本。</param>
    /// <returns>写出调用的耗时，单位毫秒。</returns>
    private long WriteFrame(byte[] frame, byte index)
    {
        Stopwatch watch = Stopwatch.StartNew();

        _link.Write(frame);

        watch.Stop();

        if (watch.ElapsedMilliseconds > SlowWriteThresholdMs)
        {
            _log($"[工具] 第 {index} 包写出耗时 {watch.ElapsedMilliseconds} ms，" +
                 "字节可能还堵在链路上，设备等不到整帧");
        }

        return watch.ElapsedMilliseconds;
    }

    /// <summary>发送 EOT 并按对端形态决定是否补发结束包，随后收取设备结论。</summary>
    private void FinishSession(CancellationToken cancellationToken)
    {
        _log("[工具] 发送 EOT");

        _link.Write([Ymodem.Eot]);

        if (WaitControl(AckTargets, FinalAckTimeoutMs, cancellationToken) != WireResponse.Ack)
        {
            throw new OtaUploadException(HandshakeFailure("EOT 未被确认"));
        }

        /* Boot 侧收到 EOT 直接 finalize，不发 'C'；App 侧会回 ACK + 'C' 索要结束包。
           用一个短窗口探测，收不到就按 Boot 侧处理，避免多发一个包污染命令行。 */
        if (WaitControl(HandshakeTargets, EndPacketProbeMs, cancellationToken) == WireResponse.Ready)
        {
            _log("[工具] 设备索要结束包（App 侧），补发");
            _link.Write(Ymodem.BuildEndPacket());

            /* 结束包本身的 ACK 不作为成功判据：Boot 收到 EOT 就已经收尾复位，永远不会
               回这个 ACK。一旦把 Boot 误判成 App 侧，在这里判死就会出现"设备明明已经
               升级成功、工具却报失败"。会话成败统一交给下面收集到的设备结论行。 */
            _ = WaitControl(AckTargets, EndPacketAckTimeoutMs, cancellationToken);
        }

        _log("[工具] 等待设备结论");
        CollectReport(ReportWaitMs, cancellationToken);
    }

    /// <summary>在给定窗口内持续收取设备文本行，直到出现 OK/ERR 结论。</summary>
    private void CollectReport(int timeoutMs, CancellationToken cancellationToken)
    {
        Stopwatch watch = Stopwatch.StartNew();

        while ((_lastReply.Length == 0) && (watch.ElapsedMilliseconds < timeoutMs))
        {
            cancellationToken.ThrowIfCancellationRequested();

            int remaining = (int)(timeoutMs - watch.ElapsedMilliseconds);
            if (!_link.TryReadByte(remaining, out byte value))
            {
                break;
            }

            FeedText(value);
        }
    }

    /// <summary>对照设备回报的长度与 CRC32 与本地计算结果。</summary>
    private void VerifyReported(uint localCrc32, int imageSize)
    {
        if (_lastReply.Length == 0)
        {
            throw new OtaUploadException("数据传输结束，但设备没有给出结论行。");
        }

        if (_lastReply.StartsWith("ERR", StringComparison.Ordinal))
        {
            throw new OtaUploadException($"设备拒绝本次升级：{_lastReply}");
        }

        bool sizeMatches = !TryGetField(_lastReply, "size", out uint reportedSize) ||
                           (reportedSize == (uint)imageSize);
        uint? reportedCrc = ReadCrcField(_lastReply);
        bool crcMatches = !reportedCrc.HasValue || (reportedCrc.Value == localCrc32);

        if (sizeMatches && crcMatches)
        {
            _log($"[工具] 核对通过：长度 {imageSize} 字节，CRC32=0x{localCrc32:X8}");
            return;
        }

        throw new OtaUploadException(
            $"设备回报与本地计算不一致：{_lastReply}（本地 size={imageSize} crc32=0x{localCrc32:X8}）");
    }

    /// <summary>等待目标控制字之一，期间把文本字节交给行解析器。</summary>
    private WireResponse WaitControl(WireResponse[] targets, int timeoutMs, CancellationToken cancellationToken)
    {
        Stopwatch watch = Stopwatch.StartNew();

        while (watch.ElapsedMilliseconds < timeoutMs)
        {
            cancellationToken.ThrowIfCancellationRequested();

            int remaining = (int)(timeoutMs - watch.ElapsedMilliseconds);
            if (remaining <= 0)
            {
                break;
            }

            if (!_link.TryReadByte(remaining, out byte value))
            {
                break;
            }

            WireResponse? mapped = MapControl(value);
            if (mapped is null)
            {
                FeedText(value);
                if (_deviceError.Length > 0)
                {
                    return WireResponse.DeviceError;
                }

                continue;
            }

            if (targets.Contains(mapped.Value))
            {
                return mapped.Value;
            }

            /* 收到了控制字但不是本轮等待的目标：原样打出来，便于区分"对端主动
               发 CAN 拒绝"与"链路超时"——两者此前都被归一成同一句"超时"。 */
            _log($"[工具] 收到非目标控制字 0x{value:X2}（{Describe(mapped.Value)}）");

            /* CAN 无论是否在目标集合里都代表对端主动中止，一律立刻上报。 */
            if (mapped.Value == WireResponse.Can)
            {
                return WireResponse.Can;
            }
        }

        return WireResponse.Timeout;
    }

    /// <summary>把收到的字节交给行解析器，并记住应答行。</summary>
    private void FeedText(byte value)
    {
        string? line = _lines.Feed(value);

        if (line is null)
        {
            return;
        }

        _log($"[设备] {line}");

        if (line.StartsWith("OK", StringComparison.Ordinal))
        {
            _lastReply = line;
        }
        else if (line.StartsWith("ERR", StringComparison.Ordinal))
        {
            _lastReply = line;
            _deviceError = line;
        }
    }

    /// <summary>把线上字节映射成控制字，非控制字节返回 null。</summary>
    private static WireResponse? MapControl(byte value) => value switch
    {
        Ymodem.Ack => WireResponse.Ack,
        Ymodem.Nak => WireResponse.Nak,
        Ymodem.Can => WireResponse.Can,
        Ymodem.C => WireResponse.Ready,
        _ => null,
    };

    /// <summary>设备全程无应答时的提示：掉电、复位、串口被占用都是同一副面孔。</summary>
    private const string NoResponseHint =
        "设备全程无响应，请检查供电与串口连接（掉电、复位、串口被其它程序占用都会是这种表现）。";

    /// <summary>构造失败信息，设备已经报错时优先引用设备的结论。</summary>
    /// <param name="stage">失败卡在哪一步。</param>
    /// <remarks>
    /// 走到这里又没有设备错误行，说明整个等待窗口里一个字节都没收到——这正是
    /// 掉电的样子，所以补一句可操作的提示，而不是只留一个干巴巴的"超时"。
    /// </remarks>
    private string HandshakeFailure(string stage)
    {
        if (_deviceError.Length > 0)
        {
            return $"设备拒绝本次升级：{_deviceError}";
        }

        return $"{stage}超时。{NoResponseHint}";
    }

    /// <summary>按应答类型补充可操作的提示。</summary>
    /// <param name="response">本阶段最后收到的应答。</param>
    /// <returns>超时时返回掉电提示，其余情况返回空串。</returns>
    /// <remarks>
    /// 只有"超时"才指向对端消失：NAK 说明设备还活着但收不下这一包（更可能是内容
    /// 或波特率的问题），这时提掉电会把排查方向带偏。
    /// </remarks>
    private static string HintFor(WireResponse response) =>
        (response == WireResponse.Timeout) ? NoResponseHint : string.Empty;

    /// <summary>把控制字描述成可读文本。</summary>
    private static string Describe(WireResponse response) => response switch
    {
        WireResponse.Nak => "NAK",
        WireResponse.Can => "对端中止 CAN",
        WireResponse.DeviceError => "设备报错",
        WireResponse.Timeout => "超时",
        WireResponse.Ack => "ACK",
        _ => "未知应答",
    };

    /// <summary>从 "<c>k=v</c>" 应答行里取一个十进制字段。</summary>
    private static bool TryGetField(string line, string key, out uint value)
    {
        value = 0;

        foreach (string token in line.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            int separator = token.IndexOf('=', StringComparison.Ordinal);
            if (separator <= 0 || !token.AsSpan(0, separator).SequenceEqual(key))
            {
                continue;
            }

            return uint.TryParse(token.AsSpan(separator + 1), NumberStyles.None,
                                 CultureInfo.InvariantCulture, out value);
        }

        return false;
    }

    /// <summary>取应答行里的文本字段，字段不存在时返回 null。</summary>
    /// <param name="line">应答行。</param>
    /// <param name="key">字段名，如 <c>slot</c>。</param>
    private static string? ReadTextField(string line, string key)
    {
        foreach (string token in line.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            int separator = token.IndexOf('=', StringComparison.Ordinal);
            if (separator <= 0 || !token.AsSpan(0, separator).SequenceEqual(key))
            {
                continue;
            }

            return token[(separator + 1)..];
        }

        return null;
    }

    /// <summary>把应答行里的 CRC 字段取出来，兼容 App 的十进制 <c>crc32</c> 与 Boot 的十六进制 <c>crc</c>。</summary>
    private static uint? ReadCrcField(string line)
    {
        foreach (string key in (string[])["crc32", "crc"])
        {
            foreach (string token in line.Split(' ', StringSplitOptions.RemoveEmptyEntries))
            {
                int separator = token.IndexOf('=', StringComparison.Ordinal);
                if (separator <= 0 || !token.AsSpan(0, separator).SequenceEqual(key))
                {
                    continue;
                }

                ReadOnlySpan<char> text = token.AsSpan(separator + 1);
                if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    return uint.TryParse(text[2..], NumberStyles.HexNumber,
                                         CultureInfo.InvariantCulture, out uint hex) ? hex : null;
                }

                return uint.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out uint dec)
                    ? dec
                    : null;
            }
        }

        return null;
    }
}

/// <summary>线上控制字的语义化表示。</summary>
internal enum WireResponse
{
    /// <summary>等待超时。</summary>
    Timeout,

    /// <summary>收到 ACK。</summary>
    Ack,

    /// <summary>收到 NAK，应重传当前包。</summary>
    Nak,

    /// <summary>收到 CAN，对端主动中止。</summary>
    Can,

    /// <summary>收到 'C'，对端请求开始 / 索要结束包。</summary>
    Ready,

    /// <summary>设备输出了一行 ERR，会话已被拒绝。</summary>
    DeviceError,
}

/// <summary>升级进度快照。</summary>
/// <param name="Sent">已发送字节数。</param>
/// <param name="Total">镜像总字节数。</param>
/// <param name="Stage">当前阶段描述。</param>
internal readonly record struct UploadProgress(int Sent, int Total, string Stage);

/// <summary>
/// 镜像自检：从裸 .bin 的向量表反推它是为哪个槽链接的。
/// </summary>
/// <remarks>
/// 两个槽的链接地址不同（载荷分别落在 0x08020200 与 0x08080200），同一份 .bin
/// 不能互换：装错槽的镜像即使字节完全正确，也会在设备侧的向量表预检里被拒。
/// 这里只读头 8 字节，用复位向量落点判断归属，属于"够用就好"的启发式——
/// 判不出来时一律返回 null，由调用方决定是否需要拦住。
/// </remarks>
internal static class ImageProbe
{
    /* 与 boot_conf.h / app_config.h 对齐的分区常量，改分区表时必须同步。 */
    private const uint SlotAPayload = 0x08020200u;     // Slot A 载荷起点（槽首 + 0x200）
    private const uint SlotAEnd = 0x08080000u;         // Slot A 载荷上界（也是 Slot B 槽首）
    private const uint SlotBPayload = 0x08080200u;     // Slot B 载荷起点（槽首 + 0x200）
    private const uint SlotBEnd = 0x080E0000u;         // Slot B 载荷上界（预留区起点）
    private const uint RamBase = 0x20000000u;          // SRAM 起始，初始 MSP 下限
    private const uint RamEnd = 0x20020000u;           // SRAM 结束（不含），初始 MSP 上限

    /// <summary>判断镜像属于哪个槽。</summary>
    /// <param name="image">镜像字节（只用到前 8 字节）。</param>
    /// <returns>"A"、"B"，或无法判定时返回 null。</returns>
    public static string? DetectSlot(ReadOnlySpan<byte> image)
    {
        if (!TryReadVectors(image, out _, out uint pc) || ((pc & 0x1u) == 0u))
        {
            return null;
        }

        uint entry = pc & ~0x1u;

        if ((entry >= SlotAPayload) && (entry < SlotAEnd))
        {
            return "A";
        }

        if ((entry >= SlotBPayload) && (entry < SlotBEnd))
        {
            return "B";
        }

        return null;
    }

    /// <summary>把镜像头部的向量表描述成一行给人看的话。</summary>
    /// <param name="image">镜像字节（只用到前 8 字节）。</param>
    public static string Describe(ReadOnlySpan<byte> image)
    {
        if (!TryReadVectors(image, out uint sp, out uint pc))
        {
            return "文件不足 8 字节，读不出向量表。";
        }

        string head = $"SP=0x{sp:X8}，复位向量=0x{pc:X8}";

        if (((sp & 0x3u) != 0u) || (sp < RamBase) || (sp > RamEnd))
        {
            return $"{head} → 无效：SP 不在 SRAM（0x{RamBase:X8}~0x{RamEnd:X8}）内。";
        }

        if ((pc & 0x1u) == 0u)
        {
            return $"{head} → 无效：复位向量不是 Thumb 地址（最低位应为 1）。";
        }

        string? slot = DetectSlot(image);

        if (slot == "A")
        {
            return $"{head} → Slot A 镜像（链接在 0x{SlotAPayload:X8}）。";
        }

        if (slot == "B")
        {
            return $"{head} → Slot B 镜像（链接在 0x{SlotBPayload:X8}）。";
        }

        return $"{head} → 复位向量不在任何槽的载荷区间内，烧进去也跑不起来。";
    }

    /// <summary>读取向量表首两项。</summary>
    /// <param name="image">镜像字节。</param>
    /// <param name="sp">初始 MSP。</param>
    /// <param name="pc">复位向量。</param>
    /// <returns>false 表示长度不足。</returns>
    private static bool TryReadVectors(ReadOnlySpan<byte> image, out uint sp, out uint pc)
    {
        sp = 0u;
        pc = 0u;

        if (image.Length < 8)
        {
            return false;
        }

        sp = BinaryPrimitives.ReadUInt32LittleEndian(image);
        pc = BinaryPrimitives.ReadUInt32LittleEndian(image[4..]);

        return true;
    }
}