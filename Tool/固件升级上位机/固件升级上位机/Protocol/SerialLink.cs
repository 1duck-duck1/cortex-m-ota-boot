using System.Collections.Concurrent;
using System.IO.Ports;
using System.Text;

namespace 固件升级上位机;

/// <summary>
/// 串口链路的字节级收发封装：后台线程收字节，调用方按超时取字节。
/// </summary>
/// <remarks>
/// 收到的字节统一进阻塞队列，同一时刻只应有一个消费者（终端轮询或升级会话），
/// 两者由调用方按模式切换，避免互相抢数据。队列有容量上限：无人消费时丢弃新
/// 字节而不是无限增长，溢出会在 <see cref="HasOverrun"/> 上留下痕迹。
/// </remarks>
internal sealed class SerialLink : IDisposable
{
    private const int QueueCapacity = 1 << 18;

    /// <summary>接收线程没数据时的轮询间隔，单位毫秒。</summary>
    private const int IdlePollDelayMs = 1;

    /// <summary>下发命令行时的单字节间隔，单位毫秒。</summary>
    /// <remarks>
    /// 设备侧的 CLI 每收一个字符就要阻塞回显一次，单字符处理时间超过了 115200 下的
    /// 字节间隔（约 87 us），整行连续下发会让 USART2 那 1 字节的接收寄存器来不及被
    /// 取走而丢字节——表现为命令分词错乱、回 ERR reason=BAD_PARAM。按打字的速度逐
    /// 字节送出即可规避；一条 20 字符的命令只多花几十毫秒。
    /// </remarks>
    private const int CommandByteDelayMs = 2;

    private readonly BlockingCollection<byte> _queue =
        new(new ConcurrentQueue<byte>(), QueueCapacity);

    private SerialPort? _port;
    private Thread? _reader;
    private volatile bool _running;
    private volatile bool _overrun;

    /// <summary>串口是否已打开。</summary>
    public bool IsOpen => _port is { IsOpen: true };

    /// <summary>当前端口名，未打开时为 null。</summary>
    public string? PortName => _port?.PortName;

    /// <summary>接收队列是否发生过溢出（说明有字节被丢弃）。</summary>
    public bool HasOverrun => _overrun;

    /// <summary>打开串口并启动接收线程。</summary>
    /// <param name="portName">端口名，形如 COM3。</param>
    /// <param name="baudRate">波特率。</param>
    /// <exception cref="IOException">端口不存在或被占用。</exception>
    /// <exception cref="UnauthorizedAccessException">端口已被其他进程打开。</exception>
    public void Open(string portName, int baudRate)
    {
        Close();

        SerialPort port = new(portName, baudRate, Parity.None, 8, StopBits.One)
        {
            Handshake = Handshake.None,
            WriteTimeout = 5000,
            ReadBufferSize = 1 << 16,
            WriteBufferSize = 1 << 16,
        };

        port.Open();

        _port = port;
        _overrun = false;
        _running = true;
        _reader = new Thread(ReaderLoop)
        {
            IsBackground = true,
            Name = "serial-reader",
        };
        _reader.Start();
    }

    /// <summary>关闭串口并停止接收线程。</summary>
    public void Close()
    {
        _running = false;

        try
        {
            _port?.Close();
        }
        catch (IOException)
        {
            /* 拔线时关闭会抛异常，此时目标是"尽力断开"，不向上传播。 */
        }

        _port?.Dispose();
        _port = null;
        _reader = null;

        DiscardInput();
    }

    /// <summary>写出原始字节。</summary>
    /// <param name="data">待写出的字节。</param>
    /// <exception cref="InvalidOperationException">串口未打开。</exception>
    public void Write(byte[] data)
    {
        SerialPort? port = _port;

        if (port is null || !port.IsOpen)
        {
            throw new InvalidOperationException("串口未打开。");
        }

        port.Write(data, 0, data.Length);
    }

    /// <summary>写出一段 ASCII 文本。</summary>
    /// <param name="text">待写出的文本。</param>
    public void Write(string text) => Write(Encoding.ASCII.GetBytes(text));

    /// <summary>按设备命令行约定下发一条命令：正文 + '\r'，逐字节限速送出。</summary>
    /// <param name="command">命令正文，不含结尾回车。</param>
    /// <remarks>
    /// 这里不能像 YMODEM 数据帧那样整块写出：命令行会被设备逐字符回显，设备跟不上
    /// 连续输入，详见 <see cref="CommandByteDelayMs"/>。数据帧则必须整块发，否则
    /// 384 KB 的传输会被拖垮。
    /// </remarks>
    public void WriteCommand(string command)
    {
        byte[] data = Encoding.ASCII.GetBytes(command + "\r");

        foreach (byte value in data)
        {
            Write([value]);
            Thread.Sleep(CommandByteDelayMs);
        }
    }

    /// <summary>按超时取一个字节。</summary>
    /// <param name="timeoutMs">等待上限，单位毫秒。</param>
    /// <param name="value">取到的字节。</param>
    /// <returns>true 表示在超时前取到了字节。</returns>
    public bool TryReadByte(int timeoutMs, out byte value)
    {
        value = 0;

        if (timeoutMs <= 0)
        {
            return false;
        }

        try
        {
            return _queue.TryTake(out value, timeoutMs);
        }
        catch (InvalidOperationException)
        {
            /* 队列已结束（串口关闭），按超时处理。 */
            return false;
        }
    }

    /// <summary>取走当前已到达的全部字节。</summary>
    /// <param name="sink">收取目标，按到达顺序追加。</param>
    /// <returns>取走的字节数。</returns>
    public int Drain(List<byte> sink)
    {
        int count = 0;

        while (_queue.TryTake(out byte value))
        {
            sink.Add(value);
            count++;
        }

        return count;
    }

    /// <summary>丢弃尚未取走的接收字节。</summary>
    /// <remarks>
    /// 每次切换消费者之前都要调用：残留的半个命令行或上一轮的握手 'C'
    /// 会被新消费者当成自己的输入。
    /// </remarks>
    public void DiscardInput()
    {
        while (_queue.TryTake(out _))
        {
            /* 丢弃即可。 */
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        Close();
        _queue.Dispose();
    }

    /// <summary>后台接收线程：串口读到的字节一律进队列。</summary>
    /// <remarks>
    /// 刻意不用带超时的阻塞读：没数据时 <c>Read</c> 会抛 <c>TimeoutException</c>，
    /// 虽然被吞掉了不影响行为，但会在调试器里每秒刷出十几次"第一次机会异常"，
    /// 既淹没真正的问题也让人误以为出了故障。改成先问驱动缓冲里有多少字节、
    /// 没有就短睡一下：不抛异常，关闭串口后也能在 1 ms 内退出循环。
    /// </remarks>
    private void ReaderLoop()
    {
        byte[] buffer = new byte[1024];

        while (_running)
        {
            SerialPort? port = _port;

            if (port is null || !port.IsOpen)
            {
                break;
            }

            try
            {
                int available = port.BytesToRead;

                if (available == 0)
                {
                    Thread.Sleep(IdlePollDelayMs);
                    continue;
                }

                int count = port.Read(buffer, 0, Math.Min(available, buffer.Length));

                for (int index = 0; index < count; index++)
                {
                    if (!_queue.TryAdd(buffer[index]))
                    {
                        _overrun = true;
                    }
                }
            }
            catch (Exception)
            {
                /* 端口被关闭或拔线：结束接收线程，由上层决定如何提示。 */
                break;
            }
        }
    }
}