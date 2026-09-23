namespace 固件升级上位机;

/// <summary>
/// YMODEM 帧内校验用的 CRC16（poly 0x1021，初值 0）。
/// </summary>
/// <remarks>
/// 与设备侧 <c>ota_ymodem_crc16_update()</c> 同一口径：只覆盖数据体，
/// 不含帧首、包序号与序号反码。把序号算进去会导致与所有标准实现都对不上校验。
/// </remarks>
internal static class Crc16
{
    /// <summary>计算一段数据的 CRC16。</summary>
    /// <param name="data">待计算的数据体。</param>
    /// <returns>CRC16 结果。</returns>
    public static ushort Compute(ReadOnlySpan<byte> data)
    {
        ushort crc = 0;

        foreach (byte value in data)
        {
            crc ^= (ushort)(value << 8);
            for (int bit = 0; bit < 8; bit++)
            {
                if ((crc & 0x8000) != 0)
                {
                    crc = (ushort)((crc << 1) ^ 0x1021);
                }
                else
                {
                    crc = (ushort)(crc << 1);
                }
            }
        }

        return crc;
    }
}