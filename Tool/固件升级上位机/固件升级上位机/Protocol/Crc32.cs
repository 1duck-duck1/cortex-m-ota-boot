namespace 固件升级上位机;

/// <summary>
/// 与设备侧 <c>ota_crc32.c</c> 逐位一致的 CRC32（poly 0xEDB88320，init / xorout 均为 0xFFFFFFFF）。
/// </summary>
/// <remarks>
/// 等价于 <c>zlib.crc32</c>。设备回报的 <c>size</c> / <c>crc32</c> 就是拿这个口径算的，
/// 上位机必须用同一套算法才能对照落盘是否真的正确。
/// </remarks>
internal static class Crc32
{
    private const uint InitValue = 0xFFFFFFFFu;
    private const uint XorOutValue = 0xFFFFFFFFu;

    private static readonly uint[] Table = BuildTable();

    /// <summary>计算一段数据的最终 CRC32。</summary>
    /// <param name="data">待计算的数据。</param>
    /// <returns>CRC32 结果。</returns>
    public static uint Compute(ReadOnlySpan<byte> data)
    {
        return Update(InitValue, data) ^ XorOutValue;
    }

    /// <summary>增量计算 CRC32 的中间值。</summary>
    /// <param name="crc">上一段返回的中间值，首段传 0xFFFFFFFF。</param>
    /// <param name="data">本段数据。</param>
    /// <returns>新的中间值，须再异或 0xFFFFFFFF 才是最终结果。</returns>
    public static uint Update(uint crc, ReadOnlySpan<byte> data)
    {
        foreach (byte value in data)
        {
            crc = (crc >> 8) ^ Table[(crc ^ value) & 0xFF];
        }

        return crc;
    }

    /// <summary>构造反射多项式 0xEDB88320 的 256 项查表。</summary>
    private static uint[] BuildTable()
    {
        uint[] table = new uint[256];

        for (uint index = 0; index < table.Length; index++)
        {
            uint value = index;
            for (int bit = 0; bit < 8; bit++)
            {
                value = ((value & 1u) != 0) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }

            table[index] = value;
        }

        return table;
    }
}