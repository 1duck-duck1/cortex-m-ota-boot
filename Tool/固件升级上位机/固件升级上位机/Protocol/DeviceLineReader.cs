using System.Text;

namespace 固件升级上位机;

/// <summary>
/// 把设备发来的字节流还原成一行行文本，并抑制命令回显。
/// </summary>
/// <remarks>
/// 设备侧 <c>ota_cli_feed()</c> 会逐字符回显录入的命令，并在回车后补 "\r\n"，
/// 因此上位机原样记录会看到每条命令重复两遍。这里在发出命令时登记一份"预期
/// 回显"，收到与之逐字符相同的字节就直接吞掉；一旦不匹配立刻放弃抑制，
/// 保证不误吞真正的应答。
/// </remarks>
internal sealed class DeviceLineReader
{
    private readonly StringBuilder _line = new();
    private string _expectedEcho = string.Empty;

    /// <summary>登记一段预期回显，通常是刚发出的命令正文。</summary>
    /// <param name="text">刚下发的命令正文，不含结尾回车。</param>
    public void ExpectEcho(string text) => _expectedEcho += text;

    /// <summary>喂入一个字节。</summary>
    /// <param name="value">收到的字节。</param>
    /// <returns>凑齐一整行时返回该行文本（已去掉 CR/LF），否则返回 null。</returns>
    public string? Feed(byte value)
    {
        if (_expectedEcho.Length > 0)
        {
            if (value == (byte)_expectedEcho[0])
            {
                _expectedEcho = _expectedEcho[1..];
                return null;
            }

            /* 与预期不符：对端没有回显或回显被截断，立即停止抑制，避免吞掉正文。 */
            _expectedEcho = string.Empty;
        }

        if (value == (byte)'\r')
        {
            return FlushLine();
        }

        if (value == (byte)'\n')
        {
            /* 设备回显回车时写的是 "\r\n"，'\n' 只在行缓冲里还有内容时才收尾。 */
            return (_line.Length > 0) ? FlushLine() : null;
        }

        if ((value == 0x08) || (value == 0x7F))
        {
            if (_line.Length > 0)
            {
                _line.Length--;
            }

            return null;
        }

        if (value is >= 0x20 and <= 0x7E)
        {
            _line.Append((char)value);
        }

        return null;
    }

    /// <summary>取出一行并清空行缓冲。</summary>
    private string? FlushLine()
    {
        if (_line.Length == 0)
        {
            return null;
        }

        string text = _line.ToString();
        _line.Clear();

        return text;
    }
}