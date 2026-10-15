namespace 固件升级上位机
{
    partial class Form1
    {
        /// <summary>
        ///  Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        ///  Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        ///  Required method for Designer support - do not modify
        ///  the contents of this method with the code editor.
        /// </summary>
        /// <remarks>
        /// 布局全部走 TableLayoutPanel：需要随窗口伸缩的输入框必须放在百分比列里，
        /// 放在 FlowLayoutPanel 里只能得到固定宽度（Flow 布局不会横向拉伸子控件）。
        /// 所有按钮都是 AutoSize，宽度由文字决定，避免"ota erase A"这类较长文本被截断。
        /// </remarks>
        private void InitializeComponent()
        {
            components = new System.ComponentModel.Container();
            rootLayout = new TableLayoutPanel();
            settingsPanel = new TableLayoutPanel();
            portRow = new TableLayoutPanel();
            lblPort = new Label();
            cmbPort = new ComboBox();
            lblBaud = new Label();
            cmbBaud = new ComboBox();
            btnRefresh = new Button();
            btnOpen = new Button();
            lblPortState = new Label();
            fileRow = new TableLayoutPanel();
            lblFile = new Label();
            txtFile = new TextBox();
            btnBrowse = new Button();
            lblVersion = new Label();
            txtVersion = new TextBox();
            btnStart = new Button();
            btnCancel = new Button();
            progressPanel = new TableLayoutPanel();
            lblProgress = new Label();
            progressBar = new ProgressBar();
            txtLog = new TextBox();
            terminalPanel = new TableLayoutPanel();
            commandRow = new TableLayoutPanel();
            lblCommand = new Label();
            txtCommand = new TextBox();
            btnSend = new Button();
            btnClearLog = new Button();
            commonRow = new FlowLayoutPanel();
            lblCommonGroup = new Label();
            btnInfo = new Button();
            btnSlot = new Button();
            btnReset = new Button();
            btnHelp = new Button();
            lblBootGroup = new Label();
            btnAdoptA = new Button();
            btnAdoptB = new Button();
            appRow = new FlowLayoutPanel();
            lblAppGroup = new Label();
            btnEraseA = new Button();
            btnEraseB = new Button();
            btnConfirm = new Button();
            btnBoot = new Button();
            rootLayout.SuspendLayout();
            settingsPanel.SuspendLayout();
            portRow.SuspendLayout();
            fileRow.SuspendLayout();
            progressPanel.SuspendLayout();
            terminalPanel.SuspendLayout();
            commandRow.SuspendLayout();
            commonRow.SuspendLayout();
            appRow.SuspendLayout();
            SuspendLayout();
            // 
            // rootLayout
            // 
            rootLayout.ColumnCount = 1;
            rootLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            rootLayout.Controls.Add(settingsPanel, 0, 0);
            rootLayout.Controls.Add(progressPanel, 0, 1);
            rootLayout.Controls.Add(txtLog, 0, 2);
            rootLayout.Controls.Add(terminalPanel, 0, 3);
            rootLayout.Dock = DockStyle.Fill;
            rootLayout.Location = new Point(0, 0);
            rootLayout.Name = "rootLayout";
            rootLayout.RowCount = 4;
            rootLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 84F));
            rootLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 56F));
            rootLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            rootLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 118F));
            rootLayout.Size = new Size(980, 660);
            rootLayout.TabIndex = 0;
            // 
            // settingsPanel
            // 
            settingsPanel.ColumnCount = 1;
            settingsPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            settingsPanel.Controls.Add(portRow, 0, 0);
            settingsPanel.Controls.Add(fileRow, 0, 1);
            settingsPanel.Dock = DockStyle.Fill;
            settingsPanel.Location = new Point(0, 0);
            settingsPanel.Margin = new Padding(0);
            settingsPanel.Name = "settingsPanel";
            settingsPanel.RowCount = 2;
            settingsPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 42F));
            settingsPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 42F));
            settingsPanel.Size = new Size(980, 84);
            settingsPanel.TabIndex = 0;
            // 
            // portRow
            // 
            portRow.ColumnCount = 7;
            portRow.ColumnStyles.Add(new ColumnStyle());
            portRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 132F));
            portRow.ColumnStyles.Add(new ColumnStyle());
            portRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 112F));
            portRow.ColumnStyles.Add(new ColumnStyle());
            portRow.ColumnStyles.Add(new ColumnStyle());
            portRow.ColumnStyles.Add(new ColumnStyle());
            portRow.Controls.Add(lblPort, 0, 0);
            portRow.Controls.Add(cmbPort, 1, 0);
            portRow.Controls.Add(lblBaud, 2, 0);
            portRow.Controls.Add(cmbBaud, 3, 0);
            portRow.Controls.Add(btnRefresh, 4, 0);
            portRow.Controls.Add(btnOpen, 5, 0);
            portRow.Controls.Add(lblPortState, 6, 0);
            portRow.Dock = DockStyle.Fill;
            portRow.Location = new Point(0, 0);
            portRow.Margin = new Padding(0);
            portRow.Name = "portRow";
            portRow.RowCount = 1;
            portRow.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            portRow.Size = new Size(980, 42);
            portRow.TabIndex = 0;
            // 
            // lblPort
            // 
            lblPort.Anchor = AnchorStyles.None;
            lblPort.AutoSize = true;
            lblPort.Location = new Point(6, 12);
            lblPort.Margin = new Padding(6, 0, 2, 0);
            lblPort.Name = "lblPort";
            lblPort.Size = new Size(38, 17);
            lblPort.TabIndex = 0;
            lblPort.Text = "串口";
            // 
            // cmbPort
            // 
            cmbPort.Anchor = AnchorStyles.None;
            cmbPort.DropDownStyle = ComboBoxStyle.DropDownList;
            cmbPort.Location = new Point(50, 8);
            cmbPort.Margin = new Padding(4, 0, 4, 0);
            cmbPort.Name = "cmbPort";
            cmbPort.Size = new Size(124, 25);
            cmbPort.TabIndex = 1;
            // 
            // lblBaud
            // 
            lblBaud.Anchor = AnchorStyles.None;
            lblBaud.AutoSize = true;
            lblBaud.Location = new Point(188, 12);
            lblBaud.Margin = new Padding(8, 0, 2, 0);
            lblBaud.Name = "lblBaud";
            lblBaud.Size = new Size(50, 17);
            lblBaud.TabIndex = 2;
            lblBaud.Text = "波特率";
            // 
            // cmbBaud
            // 
            cmbBaud.Anchor = AnchorStyles.None;
            cmbBaud.DropDownStyle = ComboBoxStyle.DropDownList;
            cmbBaud.Location = new Point(244, 8);
            cmbBaud.Margin = new Padding(4, 0, 4, 0);
            cmbBaud.Name = "cmbBaud";
            cmbBaud.Size = new Size(104, 25);
            cmbBaud.TabIndex = 3;
            // 
            // btnRefresh
            // 
            btnRefresh.Anchor = AnchorStyles.None;
            btnRefresh.AutoSize = true;
            btnRefresh.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnRefresh.Location = new Point(362, 6);
            btnRefresh.Margin = new Padding(6, 0, 0, 0);
            btnRefresh.MinimumSize = new Size(64, 30);
            btnRefresh.Name = "btnRefresh";
            btnRefresh.Size = new Size(64, 30);
            btnRefresh.TabIndex = 4;
            btnRefresh.Text = "刷新";
            btnRefresh.UseVisualStyleBackColor = true;
            btnRefresh.Click += OnRefreshClick;
            // 
            // btnOpen
            // 
            btnOpen.Anchor = AnchorStyles.None;
            btnOpen.AutoSize = true;
            btnOpen.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnOpen.Location = new Point(438, 6);
            btnOpen.Margin = new Padding(12, 0, 0, 0);
            btnOpen.MinimumSize = new Size(88, 30);
            btnOpen.Name = "btnOpen";
            btnOpen.Size = new Size(88, 30);
            btnOpen.TabIndex = 5;
            btnOpen.Text = "打开串口";
            btnOpen.UseVisualStyleBackColor = true;
            btnOpen.Click += OnOpenClick;
            // 
            // lblPortState
            // 
            lblPortState.Anchor = AnchorStyles.None;
            lblPortState.AutoSize = true;
            lblPortState.ForeColor = Color.Firebrick;
            lblPortState.Location = new Point(544, 12);
            lblPortState.Margin = new Padding(14, 0, 6, 0);
            lblPortState.Name = "lblPortState";
            lblPortState.Size = new Size(50, 17);
            lblPortState.TabIndex = 6;
            lblPortState.Text = "未连接";
            // 
            // fileRow
            // 
            fileRow.ColumnCount = 7;
            fileRow.ColumnStyles.Add(new ColumnStyle());
            fileRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            fileRow.ColumnStyles.Add(new ColumnStyle());
            fileRow.ColumnStyles.Add(new ColumnStyle());
            fileRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 124F));
            fileRow.ColumnStyles.Add(new ColumnStyle());
            fileRow.ColumnStyles.Add(new ColumnStyle());
            fileRow.Controls.Add(lblFile, 0, 0);
            fileRow.Controls.Add(txtFile, 1, 0);
            fileRow.Controls.Add(btnBrowse, 2, 0);
            fileRow.Controls.Add(lblVersion, 3, 0);
            fileRow.Controls.Add(txtVersion, 4, 0);
            fileRow.Controls.Add(btnStart, 5, 0);
            fileRow.Controls.Add(btnCancel, 6, 0);
            fileRow.Dock = DockStyle.Fill;
            fileRow.Location = new Point(0, 42);
            fileRow.Margin = new Padding(0);
            fileRow.Name = "fileRow";
            fileRow.RowCount = 1;
            fileRow.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            fileRow.Size = new Size(980, 42);
            fileRow.TabIndex = 1;
            // 
            // lblFile
            // 
            lblFile.Anchor = AnchorStyles.None;
            lblFile.AutoSize = true;
            lblFile.Location = new Point(6, 12);
            lblFile.Margin = new Padding(6, 0, 2, 0);
            lblFile.Name = "lblFile";
            lblFile.Size = new Size(38, 17);
            lblFile.TabIndex = 0;
            lblFile.Text = "固件";
            // 
            // txtFile
            // 
            txtFile.Anchor = AnchorStyles.Left | AnchorStyles.Right;
            txtFile.Location = new Point(50, 8);
            txtFile.Margin = new Padding(4, 0, 4, 0);
            txtFile.Name = "txtFile";
            txtFile.PlaceholderText = "选择要升级的 .bin 镜像";
            txtFile.Size = new Size(300, 25);
            txtFile.TabIndex = 1;
            // 
            // btnBrowse
            // 
            btnBrowse.Anchor = AnchorStyles.None;
            btnBrowse.AutoSize = true;
            btnBrowse.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnBrowse.Location = new Point(362, 6);
            btnBrowse.Margin = new Padding(8, 0, 0, 0);
            btnBrowse.MinimumSize = new Size(72, 30);
            btnBrowse.Name = "btnBrowse";
            btnBrowse.Size = new Size(72, 30);
            btnBrowse.TabIndex = 2;
            btnBrowse.Text = "浏览...";
            btnBrowse.UseVisualStyleBackColor = true;
            btnBrowse.Click += OnBrowseClick;
            // 
            // lblVersion
            // 
            lblVersion.Anchor = AnchorStyles.None;
            lblVersion.AutoSize = true;
            lblVersion.Location = new Point(440, 12);
            lblVersion.Margin = new Padding(6, 0, 2, 0);
            lblVersion.Name = "lblVersion";
            lblVersion.Size = new Size(38, 17);
            lblVersion.TabIndex = 3;
            lblVersion.Text = "版本";
            // 
            // txtVersion
            // 
            txtVersion.Anchor = AnchorStyles.None;
            txtVersion.Location = new Point(486, 8);
            txtVersion.Margin = new Padding(6, 0, 6, 0);
            txtVersion.Name = "txtVersion";
            txtVersion.Size = new Size(112, 25);
            txtVersion.TabIndex = 4;
            txtVersion.Text = "0x00010002";
            // 
            // btnStart
            // 
            btnStart.Anchor = AnchorStyles.None;
            btnStart.AutoSize = true;
            btnStart.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnStart.BackColor = Color.FromArgb(0, 120, 212);
            btnStart.FlatAppearance.BorderSize = 0;
            btnStart.FlatStyle = FlatStyle.Flat;
            btnStart.ForeColor = Color.White;
            btnStart.Location = new Point(620, 6);
            btnStart.Margin = new Padding(8, 0, 0, 0);
            btnStart.MinimumSize = new Size(92, 30);
            btnStart.Name = "btnStart";
            btnStart.Size = new Size(92, 30);
            btnStart.TabIndex = 5;
            btnStart.Text = "开始升级";
            btnStart.UseVisualStyleBackColor = false;
            btnStart.Click += OnStartClick;
            // 
            // btnCancel
            // 
            btnCancel.Anchor = AnchorStyles.None;
            btnCancel.AutoSize = true;
            btnCancel.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnCancel.Enabled = false;
            btnCancel.Location = new Point(720, 6);
            btnCancel.Margin = new Padding(8, 0, 6, 0);
            btnCancel.MinimumSize = new Size(64, 30);
            btnCancel.Name = "btnCancel";
            btnCancel.Size = new Size(64, 30);
            btnCancel.TabIndex = 6;
            btnCancel.Text = "取消";
            btnCancel.UseVisualStyleBackColor = true;
            btnCancel.Click += OnCancelClick;
            // 
            // progressPanel
            // 
            progressPanel.ColumnCount = 1;
            progressPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            progressPanel.Controls.Add(lblProgress, 0, 0);
            progressPanel.Controls.Add(progressBar, 0, 1);
            progressPanel.Dock = DockStyle.Fill;
            progressPanel.Location = new Point(0, 84);
            progressPanel.Margin = new Padding(0);
            progressPanel.Name = "progressPanel";
            progressPanel.Padding = new Padding(10, 4, 10, 4);
            progressPanel.RowCount = 2;
            progressPanel.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            progressPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 22F));
            progressPanel.Size = new Size(980, 56);
            progressPanel.TabIndex = 1;
            // 
            // lblProgress
            // 
            lblProgress.Anchor = AnchorStyles.Left | AnchorStyles.Top;
            lblProgress.AutoSize = true;
            lblProgress.Location = new Point(10, 4);
            lblProgress.Margin = new Padding(0, 0, 0, 2);
            lblProgress.Name = "lblProgress";
            lblProgress.Size = new Size(44, 17);
            lblProgress.TabIndex = 0;
            lblProgress.Text = "就绪";
            // 
            // progressBar
            // 
            progressBar.Dock = DockStyle.Fill;
            progressBar.Location = new Point(10, 26);
            progressBar.Margin = new Padding(0);
            progressBar.Name = "progressBar";
            progressBar.Size = new Size(960, 22);
            progressBar.TabIndex = 1;
            // 
            // txtLog
            // 
            txtLog.BackColor = Color.FromArgb(30, 30, 30);
            txtLog.Dock = DockStyle.Fill;
            txtLog.Font = new Font("Consolas", 9.5F);
            txtLog.ForeColor = Color.Gainsboro;
            txtLog.Location = new Point(4, 140);
            txtLog.Margin = new Padding(4);
            txtLog.Multiline = true;
            txtLog.Name = "txtLog";
            txtLog.ReadOnly = true;
            txtLog.ScrollBars = ScrollBars.Vertical;
            txtLog.Size = new Size(972, 398);
            txtLog.TabIndex = 2;
            // 日志行带时间戳后普遍超过 100 列，靠横向滚动看尾巴比换行更难读。
            txtLog.WordWrap = true;
            // 
            // terminalPanel
            // 
            terminalPanel.ColumnCount = 1;
            terminalPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            terminalPanel.Controls.Add(commandRow, 0, 0);
            terminalPanel.Controls.Add(commonRow, 0, 1);
            terminalPanel.Controls.Add(appRow, 0, 2);
            terminalPanel.Dock = DockStyle.Fill;
            terminalPanel.Location = new Point(0, 542);
            terminalPanel.Margin = new Padding(0);
            terminalPanel.Name = "terminalPanel";
            terminalPanel.RowCount = 3;
            terminalPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 38F));
            terminalPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 40F));
            terminalPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 40F));
            terminalPanel.Size = new Size(980, 118);
            terminalPanel.TabIndex = 3;
            // 
            // commandRow
            // 
            commandRow.ColumnCount = 4;
            commandRow.ColumnStyles.Add(new ColumnStyle());
            commandRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            commandRow.ColumnStyles.Add(new ColumnStyle());
            commandRow.ColumnStyles.Add(new ColumnStyle());
            commandRow.Controls.Add(lblCommand, 0, 0);
            commandRow.Controls.Add(txtCommand, 1, 0);
            commandRow.Controls.Add(btnSend, 2, 0);
            commandRow.Controls.Add(btnClearLog, 3, 0);
            commandRow.Dock = DockStyle.Fill;
            commandRow.Location = new Point(0, 0);
            commandRow.Margin = new Padding(0);
            commandRow.Name = "commandRow";
            commandRow.RowCount = 1;
            commandRow.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            commandRow.Size = new Size(980, 38);
            commandRow.TabIndex = 0;
            // 
            // lblCommand
            // 
            lblCommand.Anchor = AnchorStyles.None;
            lblCommand.AutoSize = true;
            lblCommand.Location = new Point(6, 10);
            lblCommand.Margin = new Padding(6, 0, 2, 0);
            lblCommand.Name = "lblCommand";
            lblCommand.Size = new Size(38, 17);
            lblCommand.TabIndex = 0;
            lblCommand.Text = "命令";
            // 
            // txtCommand
            // 
            txtCommand.Anchor = AnchorStyles.Left | AnchorStyles.Right;
            txtCommand.Location = new Point(50, 6);
            txtCommand.Margin = new Padding(4, 0, 4, 0);
            txtCommand.Name = "txtCommand";
            txtCommand.PlaceholderText = "手工输入设备命令，回车发送（如 info / slot / ota confirm）";
            txtCommand.Size = new Size(500, 25);
            txtCommand.TabIndex = 1;
            txtCommand.KeyDown += OnCommandKeyDown;
            // 
            // btnSend
            // 
            btnSend.Anchor = AnchorStyles.None;
            btnSend.AutoSize = true;
            btnSend.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnSend.Location = new Point(790, 4);
            btnSend.Margin = new Padding(8, 0, 0, 0);
            btnSend.MinimumSize = new Size(64, 30);
            btnSend.Name = "btnSend";
            btnSend.Size = new Size(64, 30);
            btnSend.TabIndex = 2;
            btnSend.Text = "发送";
            btnSend.UseVisualStyleBackColor = true;
            btnSend.Click += OnSendClick;
            // 
            // btnClearLog
            // 
            btnClearLog.Anchor = AnchorStyles.None;
            btnClearLog.AutoSize = true;
            btnClearLog.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnClearLog.Location = new Point(862, 4);
            btnClearLog.Margin = new Padding(8, 0, 6, 0);
            btnClearLog.MinimumSize = new Size(84, 30);
            btnClearLog.Name = "btnClearLog";
            btnClearLog.Size = new Size(84, 30);
            btnClearLog.TabIndex = 3;
            btnClearLog.Text = "清空日志";
            btnClearLog.UseVisualStyleBackColor = true;
            btnClearLog.Click += OnClearLogClick;
            // 
            // commonRow
            // 
            commonRow.Controls.Add(lblCommonGroup);
            commonRow.Controls.Add(btnInfo);
            commonRow.Controls.Add(btnSlot);
            commonRow.Controls.Add(btnReset);
            commonRow.Controls.Add(btnHelp);
            commonRow.Controls.Add(lblBootGroup);
            commonRow.Controls.Add(btnAdoptA);
            commonRow.Controls.Add(btnAdoptB);
            commonRow.Dock = DockStyle.Fill;
            commonRow.Location = new Point(0, 38);
            commonRow.Margin = new Padding(0);
            commonRow.Name = "commonRow";
            commonRow.Size = new Size(980, 40);
            commonRow.TabIndex = 1;
            commonRow.WrapContents = false;
            // 
            // lblCommonGroup
            // 
            lblCommonGroup.AutoSize = true;
            lblCommonGroup.ForeColor = Color.DimGray;
            lblCommonGroup.Location = new Point(10, 12);
            lblCommonGroup.Margin = new Padding(10, 12, 4, 0);
            lblCommonGroup.Name = "lblCommonGroup";
            lblCommonGroup.Size = new Size(38, 17);
            lblCommonGroup.TabIndex = 0;
            lblCommonGroup.Text = "通用";
            // 
            // btnInfo
            // 
            btnInfo.AutoSize = true;
            btnInfo.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnInfo.Location = new Point(58, 5);
            btnInfo.Margin = new Padding(6, 5, 0, 0);
            btnInfo.MinimumSize = new Size(64, 30);
            btnInfo.Name = "btnInfo";
            btnInfo.Size = new Size(64, 30);
            btnInfo.TabIndex = 1;
            btnInfo.Text = "info";
            btnInfo.UseVisualStyleBackColor = true;
            btnInfo.Click += OnQuickCommandClick;
            // 
            // btnSlot
            // 
            btnSlot.AutoSize = true;
            btnSlot.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnSlot.Location = new Point(128, 5);
            btnSlot.Margin = new Padding(6, 5, 0, 0);
            btnSlot.MinimumSize = new Size(64, 30);
            btnSlot.Name = "btnSlot";
            btnSlot.Size = new Size(64, 30);
            btnSlot.TabIndex = 2;
            btnSlot.Text = "slot";
            btnSlot.UseVisualStyleBackColor = true;
            btnSlot.Click += OnQuickCommandClick;
            // 
            // btnReset
            // 
            btnReset.AutoSize = true;
            btnReset.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnReset.Location = new Point(198, 5);
            btnReset.Margin = new Padding(6, 5, 0, 0);
            btnReset.MinimumSize = new Size(64, 30);
            btnReset.Name = "btnReset";
            btnReset.Size = new Size(64, 30);
            btnReset.TabIndex = 3;
            btnReset.Text = "reset";
            btnReset.UseVisualStyleBackColor = true;
            btnReset.Click += OnQuickCommandClick;
            // 
            // btnHelp
            // 
            btnHelp.AutoSize = true;
            btnHelp.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnHelp.Location = new Point(268, 5);
            btnHelp.Margin = new Padding(6, 5, 0, 0);
            btnHelp.MinimumSize = new Size(64, 30);
            btnHelp.Name = "btnHelp";
            btnHelp.Size = new Size(64, 30);
            btnHelp.TabIndex = 4;
            btnHelp.Text = "help";
            btnHelp.UseVisualStyleBackColor = true;
            btnHelp.Click += OnQuickCommandClick;
            // 
            // lblBootGroup
            // 
            lblBootGroup.AutoSize = true;
            lblBootGroup.ForeColor = Color.DimGray;
            lblBootGroup.Location = new Point(358, 12);
            lblBootGroup.Margin = new Padding(26, 12, 4, 0);
            lblBootGroup.Name = "lblBootGroup";
            lblBootGroup.Size = new Size(68, 17);
            lblBootGroup.TabIndex = 5;
            lblBootGroup.Text = "Boot 专属";
            // 
            // btnAdoptA
            // 
            btnAdoptA.AutoSize = true;
            btnAdoptA.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnAdoptA.Location = new Point(436, 5);
            btnAdoptA.Margin = new Padding(6, 5, 0, 0);
            btnAdoptA.MinimumSize = new Size(96, 30);
            btnAdoptA.Name = "btnAdoptA";
            btnAdoptA.Size = new Size(96, 30);
            btnAdoptA.TabIndex = 6;
            btnAdoptA.Tag = "ota adopt A {ver}";
            btnAdoptA.Text = "ota adopt A";
            btnAdoptA.UseVisualStyleBackColor = true;
            btnAdoptA.Click += OnQuickCommandClick;
            // 
            // btnAdoptB
            // 
            btnAdoptB.AutoSize = true;
            btnAdoptB.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnAdoptB.Location = new Point(538, 5);
            btnAdoptB.Margin = new Padding(6, 5, 0, 0);
            btnAdoptB.MinimumSize = new Size(96, 30);
            btnAdoptB.Name = "btnAdoptB";
            btnAdoptB.Size = new Size(96, 30);
            btnAdoptB.TabIndex = 7;
            btnAdoptB.Tag = "ota adopt B {ver}";
            btnAdoptB.Text = "ota adopt B";
            btnAdoptB.UseVisualStyleBackColor = true;
            btnAdoptB.Click += OnQuickCommandClick;
            // 
            // appRow
            // 
            appRow.Controls.Add(lblAppGroup);
            appRow.Controls.Add(btnEraseA);
            appRow.Controls.Add(btnEraseB);
            appRow.Controls.Add(btnConfirm);
            appRow.Controls.Add(btnBoot);
            appRow.Dock = DockStyle.Fill;
            appRow.Location = new Point(0, 78);
            appRow.Margin = new Padding(0);
            appRow.Name = "appRow";
            appRow.Size = new Size(980, 40);
            appRow.TabIndex = 2;
            appRow.WrapContents = false;
            // 
            // lblAppGroup
            // 
            lblAppGroup.AutoSize = true;
            lblAppGroup.ForeColor = Color.DimGray;
            lblAppGroup.Location = new Point(10, 12);
            lblAppGroup.Margin = new Padding(10, 12, 4, 0);
            lblAppGroup.Name = "lblAppGroup";
            lblAppGroup.Size = new Size(64, 17);
            lblAppGroup.TabIndex = 0;
            lblAppGroup.Text = "App 专属";
            // 
            // btnEraseA
            // 
            btnEraseA.AutoSize = true;
            btnEraseA.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnEraseA.Location = new Point(84, 5);
            btnEraseA.Margin = new Padding(6, 5, 0, 0);
            btnEraseA.MinimumSize = new Size(96, 30);
            btnEraseA.Name = "btnEraseA";
            btnEraseA.Size = new Size(96, 30);
            btnEraseA.TabIndex = 1;
            btnEraseA.Text = "ota erase A";
            btnEraseA.UseVisualStyleBackColor = true;
            btnEraseA.Click += OnQuickCommandClick;
            // 
            // btnEraseB
            // 
            btnEraseB.AutoSize = true;
            btnEraseB.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnEraseB.Location = new Point(186, 5);
            btnEraseB.Margin = new Padding(6, 5, 0, 0);
            btnEraseB.MinimumSize = new Size(96, 30);
            btnEraseB.Name = "btnEraseB";
            btnEraseB.Size = new Size(96, 30);
            btnEraseB.TabIndex = 2;
            btnEraseB.Text = "ota erase B";
            btnEraseB.UseVisualStyleBackColor = true;
            btnEraseB.Click += OnQuickCommandClick;
            // 
            // btnConfirm
            // 
            btnConfirm.AutoSize = true;
            btnConfirm.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnConfirm.Location = new Point(288, 5);
            btnConfirm.Margin = new Padding(6, 5, 0, 0);
            btnConfirm.MinimumSize = new Size(92, 30);
            btnConfirm.Name = "btnConfirm";
            btnConfirm.Size = new Size(92, 30);
            btnConfirm.TabIndex = 3;
            btnConfirm.Text = "ota confirm";
            btnConfirm.UseVisualStyleBackColor = true;
            btnConfirm.Click += OnQuickCommandClick;
            // 
            // btnBoot
            // 
            btnBoot.AutoSize = true;
            btnBoot.AutoSizeMode = AutoSizeMode.GrowAndShrink;
            btnBoot.Location = new Point(386, 5);
            btnBoot.Margin = new Padding(6, 5, 0, 0);
            btnBoot.MinimumSize = new Size(64, 30);
            btnBoot.Name = "btnBoot";
            btnBoot.Size = new Size(64, 30);
            btnBoot.TabIndex = 4;
            btnBoot.Text = "boot";
            btnBoot.UseVisualStyleBackColor = true;
            btnBoot.Click += OnQuickCommandClick;
            // 
            // Form1
            // 
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(980, 660);
            Controls.Add(rootLayout);
            // 最小宽度必须容下最宽的按钮行（通用 + Boot 专属两个分组，
            // 见 commonRow），否则 FlowLayoutPanel 会把 "ota adopt B" 裁掉一截。
            MinimumSize = new Size(750, 620);
            Name = "Form1";
            StartPosition = FormStartPosition.CenterScreen;
            Text = "固件升级上位机（YMODEM / 串口）";
            FormClosing += OnFormClosing;
            rootLayout.ResumeLayout(false);
            settingsPanel.ResumeLayout(false);
            portRow.ResumeLayout(false);
            portRow.PerformLayout();
            fileRow.ResumeLayout(false);
            fileRow.PerformLayout();
            progressPanel.ResumeLayout(false);
            progressPanel.PerformLayout();
            terminalPanel.ResumeLayout(false);
            commandRow.ResumeLayout(false);
            commandRow.PerformLayout();
            commonRow.ResumeLayout(false);
            commonRow.PerformLayout();
            appRow.ResumeLayout(false);
            appRow.PerformLayout();
            ResumeLayout(false);
        }

        #endregion

        private TableLayoutPanel rootLayout;
        private TableLayoutPanel settingsPanel;
        private TableLayoutPanel portRow;
        private Label lblPort;
        private ComboBox cmbPort;
        private Label lblBaud;
        private ComboBox cmbBaud;
        private Button btnRefresh;
        private Button btnOpen;
        private Label lblPortState;
        private TableLayoutPanel fileRow;
        private Label lblFile;
        private TextBox txtFile;
        private Button btnBrowse;
        private Label lblVersion;
        private TextBox txtVersion;
        private Button btnStart;
        private Button btnCancel;
        private TableLayoutPanel progressPanel;
        private Label lblProgress;
        private ProgressBar progressBar;
        private TextBox txtLog;
        private TableLayoutPanel terminalPanel;
        private TableLayoutPanel commandRow;
        private Label lblCommand;
        private TextBox txtCommand;
        private Button btnSend;
        private Button btnClearLog;
        private FlowLayoutPanel commonRow;
        private Label lblCommonGroup;
        private Button btnInfo;
        private Button btnSlot;
        private Button btnReset;
        private Button btnHelp;
        private Label lblBootGroup;
        private Button btnAdoptA;
        private Button btnAdoptB;
        private FlowLayoutPanel appRow;
        private Label lblAppGroup;
        private Button btnEraseA;
        private Button btnEraseB;
        private Button btnConfirm;
        private Button btnBoot;
    }
}