using System.Globalization;
using System.IO.Ports;

namespace 固件升级上位机
{
    /// <summary>
    /// 固件升级上位机主窗体：串口设置、YMODEM 升级、日志与手工命令行。
    /// </summary>
    /// <remarks>
    /// 串口字节只有一个消费者：升级会话进行时由 <see cref="OtaUploader"/> 独占，
    /// 空闲时由界面轮询取走渲染成日志。两者用 <c>_busy</c> 做互斥，避免抢字节。
    /// </remarks>
    public partial class Form1 : Form
    {
        private static readonly string[] BaudRates =
        [
            "9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600",
        ];

        private readonly SerialLink _link = new();
        private readonly DeviceLineReader _lines = new();
        private readonly System.Windows.Forms.Timer _pollTimer = new();
        private readonly List<byte> _drainBuffer = new();

        private CancellationTokenSource? _uploadCts;
        private bool _busy;

        /// <summary>构造主窗体并初始化串口列表与轮询定时器。</summary>
        public Form1()
        {
            InitializeComponent();

            cmbBaud.Items.AddRange(BaudRates);
            cmbBaud.SelectedItem = "115200";

            _pollTimer.Interval = 100;
            _pollTimer.Tick += OnPollTick;
            _pollTimer.Start();

            RefreshPorts();
            UpdateControlStates();

            AppendLog("[工具] 设备侧需停在命令行：Boot 的升级模式，或 App 运行态。");
            AppendLog("[工具] 升级流程：下发 ota recv 0x版本 → 等 'C' → 发镜像 → 设备自行校验并写 NVS/Metadata。");
        }

        /// <summary>切换串口开关。</summary>
        private void OnOpenClick(object? sender, EventArgs e)
        {
            if (_busy)
            {
                return;
            }

            if (_link.IsOpen)
            {
                _link.Close();
                AppendLog("[工具] 串口已关闭。");
                UpdateControlStates();
                return;
            }

            if (cmbPort.SelectedItem is not string portName)
            {
                MessageBox.Show(this, "请先选择串口。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            int baudRate = int.Parse((string)cmbBaud.SelectedItem!, CultureInfo.InvariantCulture);

            try
            {
                _link.Open(portName, baudRate);
                AppendLog($"[工具] 已打开 {portName} @ {baudRate} 8N1。");
            }
            catch (Exception exception) when (exception is IOException
                                              or UnauthorizedAccessException
                                              or ArgumentException)
            {
                MessageBox.Show(this, exception.Message, "打开串口失败",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }

            UpdateControlStates();
        }

        /// <summary>重新枚举可用串口。</summary>
        private void OnRefreshClick(object? sender, EventArgs e)
        {
            RefreshPorts();
        }

        /// <summary>选择固件镜像。</summary>
        private void OnBrowseClick(object? sender, EventArgs e)
        {
            using OpenFileDialog dialog = new()
            {
                Title = "选择固件镜像",
                Filter = "固件镜像 (*.bin)|*.bin|所有文件 (*.*)|*.*",
                CheckFileExists = true,
            };

            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                txtFile.Text = dialog.FileName;
                LogImageProbe(dialog.FileName);
            }
        }

        /// <summary>读取镜像头部的向量表并打印自检结果。</summary>
        /// <param name="path">镜像文件路径。</param>
        /// <remarks>
        /// 只读前 8 字节：A/B 两个槽的链接地址不同，同一份镜像不能互换，
        /// 提前露出链接地址能让"装错槽"在选文件阶段就暴露，而不是等传完 16 KB
        /// 被设备以 VERIFY_FAILED 拒掉。读失败只提示，不阻止选择。
        /// </remarks>
        private void LogImageProbe(string path)
        {
            byte[] head = new byte[8];

            try
            {
                using FileStream stream = File.OpenRead(path);

                if (stream.ReadAtLeast(head, head.Length, throwOnEndOfStream: false) < head.Length)
                {
                    AppendLog("[工具] 镜像自检：文件不足 8 字节，读不出向量表。");
                    return;
                }
            }
            catch (IOException exception)
            {
                AppendLog($"[工具] 镜像自检失败：{exception.Message}");
                return;
            }

            AppendLog($"[工具] 镜像自检：{ImageProbe.Describe(head)}");
        }

        /// <summary>启动一次升级会话。</summary>
        private async void OnStartClick(object? sender, EventArgs e)
        {
            if (_busy)
            {
                return;
            }

            if (!_link.IsOpen)
            {
                MessageBox.Show(this, "请先打开串口。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            string path = txtFile.Text.Trim();

            if (!File.Exists(path))
            {
                MessageBox.Show(this, "请选择存在的固件文件。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            if (!TryParseVersion(txtVersion.Text, out uint version))
            {
                MessageBox.Show(this, "版本号必须是以 0x 开头的十六进制数（如 0x00010002），留空表示不声明。",
                                "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            byte[] image;

            try
            {
                image = File.ReadAllBytes(path);
            }
            catch (IOException exception)
            {
                MessageBox.Show(this, exception.Message, "读取固件失败",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            UploadRequest request = new()
            {
                Image = image,
                FileName = Path.GetFileName(path),
                Version = (version == 0u) ? null : version,
            };

            _busy = true;
            _uploadCts = new CancellationTokenSource();
            UpdateControlStates();

            /* 上一次会话的结论会一直留在界面上，直到本次第一个进度回调才被覆盖。
               握手与擦除非活动槽加起来最长要几十秒，先把结论换成中性提示，免得
               新会话一开始就挂着"升级失败"。 */
            ResetProgress("准备中…");

            string verdict = "已取消";

            try
            {
                OtaUploader uploader = new(_link, _lines, AppendLog, OnProgress);
                CancellationToken token = _uploadCts.Token;

                UploadResult result = await Task.Run(() => uploader.Upload(request, token), token);

                verdict = "升级成功";
                AppendLog($"[工具] === 升级成功 === {result.Summary}");
                MessageBox.Show(this, $"升级成功。\r\n{result.Summary}", "完成",
                                MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (OperationCanceledException)
            {
                AppendLog("[工具] 已取消本次升级。");
            }
            catch (OtaUploadException exception)
            {
                verdict = "升级失败";
                AppendLog($"[工具] === 升级失败 === {exception.Message}");
                MessageBox.Show(this, exception.Message, "升级失败",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            catch (Exception exception)
            {
                verdict = "异常中止";
                AppendLog($"[工具] === 异常 === {exception.Message}");
                MessageBox.Show(this, exception.Message, "异常",
                                MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                _uploadCts?.Dispose();
                _uploadCts = null;
                _busy = false;
                UpdateControlStates();

                /* 进度条不会自己回零：设备掉电时传输停在中途，最后那次回调的
                   百分比会一直留在界面上，看上去像"还在传"或"已经传完"。 */
                ResetProgress(verdict);

                /* 会话收尾可能留下半行或多余的握手字节，清掉再交还终端， */
                /* 否则它们会被渲染成一条无意义的日志行。 */
                _link.DiscardInput();
            }
        }

        /// <summary>把进度区恢复成与会话状态一致的样子。</summary>
        /// <param name="text">替代百分比显示的结论文本。</param>
        /// <remarks>
        /// 会话无论成功、失败还是取消，都不该把中途的百分比留在界面上：
        /// 传输停在哪一包对用户没有意义，字节数与结论在日志里都有。
        /// </remarks>
        private void ResetProgress(string text)
        {
            progressBar.Value = 0;
            lblProgress.Text = text;
        }

        /// <summary>取消进行中的升级会话。</summary>
        private void OnCancelClick(object? sender, EventArgs e)
        {
            _uploadCts?.Cancel();
        }

        /// <summary>发送手工输入的命令。</summary>
        private void OnSendClick(object? sender, EventArgs e)
        {
            string command = txtCommand.Text.Trim();

            if (command.Length == 0)
            {
                return;
            }

            txtCommand.Clear();
            SendCommandLine(command);
        }

        /// <summary>回车即发送命令。</summary>
        private void OnCommandKeyDown(object? sender, KeyEventArgs e)
        {
            if (e.KeyCode != Keys.Enter)
            {
                return;
            }

            e.Handled = true;
            e.SuppressKeyPress = true;
            string command = txtCommand.Text.Trim();

            if (command.Length == 0)
            {
                return;
            }

            txtCommand.Clear();
            SendCommandLine(command);
        }

        /// <summary>快捷命令按钮共用处理：按钮文本即命令正文。</summary>
        /// <remarks>
        /// 需要版本号参数的命令（ota adopt）把 <c>Tag</c> 作为模板，其中的
        /// <c>{ver}</c> 用"版本"框里的值填充，避免按钮文本被版本号撑得过长。
        /// </remarks>
        private void OnQuickCommandClick(object? sender, EventArgs e)
        {
            if (sender is not Button button)
            {
                return;
            }

            string command = button.Text;

            if (button.Tag is string template)
            {
                string version = txtVersion.Text.Trim();

                if (version.Length == 0)
                {
                    AppendLog($"[工具] {button.Text} 需要版本号，请先在\"版本\"框填入（如 0x00010002）。");
                    return;
                }

                command = template.Replace("{ver}", version, StringComparison.Ordinal);
            }

            SendCommandLine(command);
        }

        /// <summary>清空日志窗。</summary>
        private void OnClearLogClick(object? sender, EventArgs e)
        {
            txtLog.Clear();
        }

        /// <summary>界面轮询：只在空闲时消费收到的字节。</summary>
        private void OnPollTick(object? sender, EventArgs e)
        {
            if (_busy || !_link.IsOpen)
            {
                return;
            }

            _drainBuffer.Clear();

            if (_link.Drain(_drainBuffer) == 0)
            {
                return;
            }

            foreach (byte value in _drainBuffer)
            {
                string? line = _lines.Feed(value);

                if (line is not null)
                {
                    AppendLog($"[设备] {line}");
                }
            }
        }

        /// <summary>窗体关闭：停定时器并释放串口。</summary>
        private void OnFormClosing(object? sender, FormClosingEventArgs e)
        {
            _pollTimer.Stop();
            _uploadCts?.Cancel();
            _link.Dispose();
        }

        /// <summary>下发一条命令行命令。</summary>
        /// <param name="command">命令正文，不含结尾回车。</param>
        private void SendCommandLine(string command)
        {
            if (_busy)
            {
                AppendLog("[工具] 升级会话进行中，暂不接受手工命令。");
                return;
            }

            if (!_link.IsOpen)
            {
                MessageBox.Show(this, "请先打开串口。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            AppendLog($"[工具] > {command}");

            /* 先登记预期回显再写出：设备会逐字符回显，登记晚了会先把回显记进日志。 */
            _lines.ExpectEcho(command);

            try
            {
                _link.WriteCommand(command);
            }
            catch (Exception exception) when (exception is InvalidOperationException or IOException)
            {
                AppendLog($"[工具] 发送失败：{exception.Message}");
            }
        }

        /// <summary>刷新串口下拉框，尽量保持当前选择。</summary>
        private void RefreshPorts()
        {
            string? current = cmbPort.SelectedItem as string;

            cmbPort.Items.Clear();
            cmbPort.Items.AddRange(SerialPort.GetPortNames());

            if (cmbPort.Items.Count == 0)
            {
                return;
            }

            if (current is not null && cmbPort.Items.Contains(current))
            {
                cmbPort.SelectedItem = current;
            }
            else
            {
                cmbPort.SelectedIndex = 0;
            }
        }

        /// <summary>按"串口是否打开"与"是否在会话中"统一刷新控件可用性与状态文本。</summary>
        /// <remarks>
        /// 会话进行时串口由升级流程独占，任何会碰链路的控件都必须锁住；
        /// 与链路无关的控件（选文件、改版本号）只在会话期间锁住。
        /// </remarks>
        private void UpdateControlStates()
        {
            bool isOpen = _link.IsOpen;
            bool idle = !_busy;

            btnOpen.Text = isOpen ? "关闭串口" : "打开串口";
            btnOpen.Enabled = idle;
            btnRefresh.Enabled = idle && !isOpen;
            cmbPort.Enabled = idle && !isOpen;
            cmbBaud.Enabled = idle && !isOpen;

            btnBrowse.Enabled = idle;
            txtFile.Enabled = idle;
            txtVersion.Enabled = idle;
            btnStart.Enabled = idle && isOpen;
            btnCancel.Enabled = _busy;

            txtCommand.Enabled = idle && isOpen;
            btnSend.Enabled = idle && isOpen;

            btnInfo.Enabled = idle && isOpen;
            btnSlot.Enabled = idle && isOpen;
            btnAdoptA.Enabled = idle && isOpen;
            btnAdoptB.Enabled = idle && isOpen;
            btnEraseA.Enabled = idle && isOpen;
            btnEraseB.Enabled = idle && isOpen;
            btnConfirm.Enabled = idle && isOpen;
            btnBoot.Enabled = idle && isOpen;
            btnReset.Enabled = idle && isOpen;
            btnHelp.Enabled = idle && isOpen;

            lblPortState.Text = isOpen ? $"已连接 {_link.PortName}" : "未连接";
            lblPortState.ForeColor = isOpen ? Color.SeaGreen : Color.Firebrick;
        }

        /// <summary>刷新进度条与阶段文本。</summary>
        /// <param name="progress">进度快照。</param>
        private void OnProgress(UploadProgress progress)
        {
            if (InvokeRequired)
            {
                BeginInvoke(() => OnProgress(progress));
                return;
            }

            if (progress.Total <= 0)
            {
                return;
            }

            int percent = (int)((long)progress.Sent * 100 / progress.Total);

            progressBar.Value = Math.Clamp(percent, 0, 100);
            lblProgress.Text = $"{progress.Stage} {percent}%（{progress.Sent}/{progress.Total} 字节）";
        }

        /// <summary>追加一行日志，可从任意线程调用。</summary>
        /// <param name="text">日志内容。</param>
        /// <remarks>
        /// 时间戳在进入时取：跨线程时日志要经 BeginInvoke 排队，若等到切回 UI
        /// 线程再取，日志顺序看上去是对的，时间却整体偏晚一个排队周期。
        /// </remarks>
        private void AppendLog(string text)
        {
            string line = $"[{DateTime.Now:HH:mm:ss.fff}] {text}";

            if (InvokeRequired)
            {
                BeginInvoke(() => AppendLogLine(line));
                return;
            }

            AppendLogLine(line);
        }

        /// <summary>把已成行（含时间戳）的文本落到日志窗，只能在 UI 线程调用。</summary>
        /// <param name="line">完整日志行。</param>
        private void AppendLogLine(string line)
        {
            txtLog.AppendText(line + Environment.NewLine);
        }

        /// <summary>解析版本号文本，接受 "0x" 前缀的十六进制。</summary>
        /// <param name="text">输入文本，空串表示不声明版本。</param>
        /// <param name="version">解析结果，空串时为 0。</param>
        /// <returns>true 表示格式合法。</returns>
        private static bool TryParseVersion(string text, out uint version)
        {
            version = 0u;

            string trimmed = text.Trim();

            if (trimmed.Length == 0)
            {
                return true;
            }

            if (!trimmed.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            string digits = trimmed[2..];

            if (digits.Length is 0 or > 8)
            {
                return false;
            }

            return uint.TryParse(digits, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out version);
        }
    }
}