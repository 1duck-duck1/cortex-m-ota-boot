namespace 固件升级上位机
{
    internal static class Program
    {
        /// <summary>
        ///  The main entry point for the application.
        /// </summary>
        /// <param name="args">命令行参数；<c>--genkey &lt;路径&gt;</c> 用于生成 Ed25519 密钥。</param>
        [STAThread]
        static void Main(string[] args)
        {
            /* 本工程是 WinExe（无控制台），但仍走命令行分支：--genkey 生成密钥后直接退出，
               不启动 WinForms。公钥除打印到控制台外还写 <路径>.pub，保证用户拿得到。 */
            if ((args.Length >= 2) && string.Equals(args[0], "--genkey", StringComparison.Ordinal))
            {
                GenerateKey(args[1]);
                return;
            }

            // To customize application configuration such as set high DPI settings or default font,
            // see https://aka.ms/applicationconfiguration.
            ApplicationConfiguration.Initialize();
            Application.Run(new Form1());
        }

        /// <summary>生成一对 Ed25519 密钥：种子以 hex 写入给定路径，公钥 hex 打印并写入 <c>&lt;路径&gt;.pub</c>。</summary>
        /// <param name="path">私钥种子输出路径。</param>
        private static void GenerateKey(string path)
        {
            FirmwareSigner.GenerateKeyPair(out byte[] seed, out byte[] publicKey);

            string seedHex = Convert.ToHexString(seed);
            string publicHex = Convert.ToHexString(publicKey);

            try
            {
                File.WriteAllText(path, seedHex + Environment.NewLine);
                File.WriteAllText(path + ".pub", publicHex + Environment.NewLine);
            }
            catch (IOException exception)
            {
                Console.WriteLine($"write key failed: {exception.Message}");
                return;
            }

            Console.WriteLine($"public key: {publicHex}");
        }
    }
}
