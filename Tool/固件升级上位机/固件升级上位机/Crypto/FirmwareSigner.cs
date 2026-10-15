using System.Buffers.Binary;
using System.Security.Cryptography;
using Org.BouncyCastle.Crypto.Parameters;
using Org.BouncyCastle.Crypto.Signers;

namespace 固件升级上位机;

/// <summary>
/// 固件 Ed25519 签名：复现设备侧 <c>OTA_IMAGE_SIGNED_PREFIX_SIZE</c> 契约。
/// </summary>
/// <remarks>
/// 被签内容是 <c>SHA-512(prologue ‖ image)</c> 的 64 字节摘要，再对该摘要做
/// RFC 8032 的 Ed25519 签名（SHA-512 变体）。prologue 是镜像头前 28 字节，
/// 字段顺序与小端布局必须与设备端逐字节一致。
/// </remarks>
internal static class FirmwareSigner
{
    /// <summary>参与签名的镜像头前缀长度（<c>offsetof(header_crc32)</c>），单位字节。</summary>
    public const int PrologueLength = 28;

    /// <summary>Ed25519 签名长度，单位字节。</summary>
    public const int SignatureLength = 64;

    /// <summary>Ed25519 私钥种子长度，单位字节。</summary>
    public const int SeedLength = 32;

    /* prologue 的固定字段，取自 ota_image.h；小端写入。 */
    private const uint Magic = 0x544F4F42u;             // ASCII "BOOT"
    private const ushort HardwareId = 0x0407;           // OTA_IMAGE_HW_ID
    private const ushort HeaderVersion = 2;             // OTA_IMAGE_HEADER_VERSION
    private const uint HeaderSize = 0x200u;             // OTA_IMAGE_HEADER_SIZE
    private const uint LoadOffset = 0x200u;             // OTA_IMAGE_LOAD_OFFSET

    /// <summary>对镜像签名，返回 64 字节 Ed25519 签名。</summary>
    /// <param name="image">载荷字节，须与设备要写入槽内的内容逐字节一致。</param>
    /// <param name="version">固件版本号；参与被签内容，因此不可为空。</param>
    /// <param name="keyPath">Ed25519 私钥文件路径。</param>
    /// <returns>64 字节签名。</returns>
    /// <exception cref="OtaUploadException">镜像为空、版本号为空或密钥读取失败。</exception>
    public static byte[] Sign(byte[] image, uint? version, string keyPath)
    {
        if (image.Length == 0)
        {
            throw new OtaUploadException("镜像文件为空，无法签名。");
        }

        if (!version.HasValue)
        {
            throw new OtaUploadException("版本号为空，无法签名：版本号参与被签内容，请先填入版本号。");
        }

        Ed25519PrivateKeyParameters key = Ed25519KeyFile.Load(keyPath);

        byte[] prologue = BuildPrologue(image, version.Value);
        byte[] digest = ComputeDigest(prologue, image);

        Ed25519Signer signer = new();
        signer.Init(true, key);
        signer.BlockUpdate(digest, 0, digest.Length);

        return signer.GenerateSignature();
    }

    /// <summary>生成一对 Ed25519 密钥。</summary>
    /// <param name="seed">输出 32 字节私钥种子。</param>
    /// <param name="publicKey">输出 32 字节公钥，供写入 Boot 的 <c>boot_pubkey.c</c>。</param>
    public static void GenerateKeyPair(out byte[] seed, out byte[] publicKey)
    {
        seed = RandomNumberGenerator.GetBytes(SeedLength);

        Ed25519PrivateKeyParameters privateKey = new(seed, 0);
        publicKey = privateKey.GeneratePublicKey().GetEncoded();
    }

    /// <summary>按 ota_image.h 的字段顺序拼出 28 字节 prologue。</summary>
    /// <param name="image">载荷字节。</param>
    /// <param name="version">固件版本号。</param>
    /// <returns>长度恒为 <see cref="PrologueLength"/> 的前缀。</returns>
    private static byte[] BuildPrologue(ReadOnlySpan<byte> image, uint version)
    {
        byte[] prologue = new byte[PrologueLength];
        int offset = 0;

        BinaryPrimitives.WriteUInt32LittleEndian(prologue.AsSpan(offset), Magic);
        offset += 4;
        BinaryPrimitives.WriteUInt16LittleEndian(prologue.AsSpan(offset), HardwareId);
        offset += 2;
        BinaryPrimitives.WriteUInt16LittleEndian(prologue.AsSpan(offset), HeaderVersion);
        offset += 2;
        BinaryPrimitives.WriteUInt32LittleEndian(prologue.AsSpan(offset), HeaderSize);
        offset += 4;
        BinaryPrimitives.WriteUInt32LittleEndian(prologue.AsSpan(offset), LoadOffset);
        offset += 4;
        BinaryPrimitives.WriteUInt32LittleEndian(prologue.AsSpan(offset), (uint)image.Length);
        offset += 4;
        BinaryPrimitives.WriteUInt32LittleEndian(prologue.AsSpan(offset), Crc32.Compute(image));
        offset += 4;
        BinaryPrimitives.WriteUInt32LittleEndian(prologue.AsSpan(offset), version);

        return prologue;
    }

    /// <summary>计算 <c>SHA-512(prologue ‖ image)</c> 的 64 字节摘要。</summary>
    private static byte[] ComputeDigest(byte[] prologue, byte[] image)
    {
        using IncrementalHash hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA512);

        hash.AppendData(prologue);
        hash.AppendData(image);

        return hash.GetHashAndReset();
    }
}
