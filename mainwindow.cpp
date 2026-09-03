#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "droparea.h"
#include "fileclient.h"
#include "fileserver.h"
#include "protocol.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>

/* 从 Qt 资源系统取图标（资源在 resources/resources.qrc 中注册） */
QIcon MainWindow::iconRes(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.png").arg(name));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);   // 加载 Designer 设计的界面（全部使用布局管理器排版）

    // 布局伸缩因子（.ui 不支持逐项 stretch，在此设置：底部区域随窗口拉伸，
    // 日志区占 3 份、状态栏占 1 份），保证窗口缩放时控件自适应
    ui->mainLayout->setStretch(3, 1);        // 底部（日志+状态）区域
    ui->bottomLayout->setStretch(0, 3);      // 日志组
    ui->bottomLayout->setStretch(1, 1);      // 状态组

    // 通过 Qt 资源系统加载按钮/菜单图标
    ui->btnSettings->setIcon(iconRes(QStringLiteral("settings")));
    ui->btnListen->setIcon(iconRes(QStringLiteral("server")));
    ui->btnConnect->setIcon(iconRes(QStringLiteral("connect")));
    ui->btnOpenFile->setIcon(iconRes(QStringLiteral("send")));
    ui->btnOpenSaveDir->setIcon(iconRes(QStringLiteral("folder")));
    ui->btnSendText->setIcon(iconRes(QStringLiteral("send")));
    ui->btnExit->setIcon(iconRes(QStringLiteral("exit")));
    ui->actOpenFile->setIcon(iconRes(QStringLiteral("send")));
    ui->actExit->setIcon(iconRes(QStringLiteral("exit")));
    ui->actSettings->setIcon(iconRes(QStringLiteral("settings")));
    ui->actAbout->setIcon(iconRes(QStringLiteral("app")));

    // 传输管理类（Model 层）：与界面显示分离
    m_server = new FileServer(this);
    m_client = new FileClient(this);

    // 状态栏右侧常驻信息
    m_statusLabel = new QLabel(QStringLiteral("就绪"), this);
    ui->statusbar->addPermanentWidget(m_statusLabel);

    loadSettings();

    /* ==================== 信号与槽连接 ==================== */

    // ---- 1) 按钮与输入框 ----
    connect(ui->btnSettings, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(ui->btnListen,   &QPushButton::clicked, this, &MainWindow::onToggleListen);
    connect(ui->btnConnect,  &QPushButton::clicked, this, &MainWindow::onToggleConnect);
    connect(ui->btnOpenFile, &QPushButton::clicked, this, &MainWindow::onOpenFileToSend);
    connect(ui->btnOpenSaveDir, &QPushButton::clicked, this, &MainWindow::onOpenSaveDir);
    connect(ui->btnExit,     &QPushButton::clicked, this, &QWidget::close);
    connect(ui->btnSendText, &QPushButton::clicked, this, &MainWindow::onSendTextClicked);
    connect(ui->leMessage,   &QLineEdit::returnPressed, this, &MainWindow::onSendTextClicked);

    // ---- 2) 菜单动作 ----
    connect(ui->actOpenFile,  &QAction::triggered, this, &MainWindow::onOpenFileToSend);
    connect(ui->actExit,      &QAction::triggered, this, &QWidget::close);
    connect(ui->actSettings,  &QAction::triggered, this, &MainWindow::openSettings);
    connect(ui->actAbout,     &QAction::triggered, this, &MainWindow::onAbout);

    // ---- 3) 服务端（FileServer）信号 -> 界面刷新 ----
    connect(m_server, &FileServer::stateChanged,       this, &MainWindow::onServerStateChanged);
    connect(m_server, &FileServer::clientConnected,    this, &MainWindow::onClientConnected);
    connect(m_server, &FileServer::clientDisconnected, this, &MainWindow::onClientDisconnected);
    connect(m_server, &FileServer::recvStarted,        this, &MainWindow::onRecvStarted);
    connect(m_server, &FileServer::recvProgress,       this, &MainWindow::onRecvProgress);
    connect(m_server, &FileServer::recvFinished,       this, &MainWindow::onRecvFinished);
    connect(m_server, &FileServer::recvFailed,         this, &MainWindow::onRecvFailed);
    connect(m_server, &FileServer::textReceived,       this, &MainWindow::onTextReceived);
    connect(m_server, &FileServer::logInfo,            this, &MainWindow::onLogInfo);
    connect(m_server, &FileServer::logError,           this, &MainWindow::onLogError);

    // ---- 4) 客户端（FileClient）信号 -> 界面刷新 ----
    connect(m_client, &FileClient::stateChanged,  this, &MainWindow::onClientStateChanged);
    connect(m_client, &FileClient::sendStarted,   this, &MainWindow::onSendStarted);
    connect(m_client, &FileClient::sendProgress,  this, &MainWindow::onSendProgress);
    connect(m_client, &FileClient::sendFinished,  this, &MainWindow::onSendFinished);
    connect(m_client, &FileClient::sendFailed,    this, &MainWindow::onSendFailed);
    connect(m_client, &FileClient::logInfo,       this, &MainWindow::onLogInfo);
    connect(m_client, &FileClient::logError,      this, &MainWindow::onLogError);

    // ---- 5) 拖拽区：拖入文件 -> 交给主窗口处理 ----
    connect(ui->dropArea, &DropArea::fileDropped, this, &MainWindow::onFileDropped);

    applySettingsToUi();
    updateUiState();
    updateTitle();

    appendLog(QStringLiteral("欢迎使用 Qt 文件传输工具！"), LogLevel::Ok);
    appendLog(QStringLiteral("使用说明：一个实例点击\"启动监听\"作为服务端；另一个实例在\"连接设置\"中"
                            "填写 IP/端口后点击\"连接服务器\"作为客户端。"), LogLevel::Info);
    appendLog(QStringLiteral("把文件拖进虚线框即可发送（未连接时则保存到接收目录）。"), LogLevel::Info);
}

MainWindow::~MainWindow()
{
    // 析构顺序很重要：先停止网络逻辑并解除其到本窗口的信号连接，
    // 再销毁界面。否则 FileServer/FileClient 作为子对象在 delete ui 之后
    // 才被析构，其析构过程中发出的日志/状态信号会访问已释放的控件导致崩溃。
    m_server->stopListen();
    m_client->disconnectFromServer();
    m_server->disconnect(this);
    m_client->disconnect(this);
    delete ui;
    ui = nullptr;
}

//---------------------------------------------------------------------
// 设置持久化（QSettings）
//---------------------------------------------------------------------

void MainWindow::loadSettings()
{
    QSettings s;
    m_settings.host = s.value(QStringLiteral("network/host"),
                              QStringLiteral("127.0.0.1")).toString();
    m_settings.port = quint16(s.value(QStringLiteral("network/port"), 8888).toUInt());
    const QString defaultDir = [] {
        QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (base.isEmpty())
            base = QCoreApplication::applicationDirPath();
        return base + QStringLiteral("/QtFileTransfer");
    }();
    m_settings.saveDir = s.value(QStringLiteral("paths/saveDir"), defaultDir).toString();
    m_server->setSaveDir(m_settings.saveDir);
}

void MainWindow::saveSettingsToDisk()
{
    QSettings s;
    s.setValue(QStringLiteral("network/host"), m_settings.host);
    s.setValue(QStringLiteral("network/port"), m_settings.port);
    s.setValue(QStringLiteral("paths/saveDir"), m_settings.saveDir);
}

void MainWindow::applySettingsToUi()
{
    ui->lblSaveDir->setText(QStringLiteral("保存目录：%1").arg(m_settings.saveDir));
    ui->lblSaveDir->setToolTip(m_settings.saveDir);
}

//---------------------------------------------------------------------
// 界面动作
//---------------------------------------------------------------------

/* 打开"连接设置"对话框（必做项：窗口间通信）
 * 对话框确定后通过 settingsApplied 信号把参数传回主窗口。 */
void MainWindow::openSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
        // 【跨窗口信号槽】对话框 -> 主窗口
        connect(m_settingsDialog, &SettingsDialog::settingsApplied,
                this, &MainWindow::onSettingsApplied);
    }
    m_settingsDialog->setSettings(m_settings);
    m_settingsDialog->exec();   // 模态显示
}

/* 接收设置对话框传来的参数 */
void MainWindow::onSettingsApplied(const AppSettings &settings)
{
    const bool portChanged = (m_settings.port != settings.port);
    m_settings = settings;
    m_server->setSaveDir(m_settings.saveDir);   // 保存目录立即生效
    applySettingsToUi();
    saveSettingsToDisk();
    appendLog(QStringLiteral("连接设置已更新：服务器 %1:%2，保存目录 %3")
                  .arg(m_settings.host).arg(m_settings.port).arg(m_settings.saveDir),
              LogLevel::Ok);
    if (portChanged && m_listening)
        appendLog(QStringLiteral("提示：监听端口已修改，请先停止监听再重新启动以生效。"),
                  LogLevel::Warn);
}

void MainWindow::onToggleListen()
{
    if (m_listening) {
        m_server->stopListen();
    } else {
        if (!m_server->startListen(m_settings.port))
            QMessageBox::warning(this, QStringLiteral("监听失败"),
                                 QStringLiteral("端口 %1 监听失败，请检查端口是否被占用。\n\n"
                                                "详细信息见日志。").arg(m_settings.port));
    }
}

void MainWindow::onToggleConnect()
{
    if (m_connected) {
        m_client->disconnectFromServer();   // 已连接 -> 断开
        return;
    }
    if (m_client->socketState() != QAbstractSocket::UnconnectedState)
        return;                             // 正在连接中，忽略重复点击
    if (m_settings.host.trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先在\"连接设置\"中填写服务器 IP。"));
        return;
    }
    m_client->connectToServer(m_settings.host, m_settings.port);
}

void MainWindow::onOpenFileToSend()
{
    const QString filter = QStringLiteral(
        "文本文件 (*.txt *.log *.csv);;图片文件 (*.png *.jpg *.jpeg *.bmp);;所有文件 (*)");
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择要发送的文件"), QString(), filter);
    if (path.isEmpty())
        return;
    m_client->sendFile(path);
}

void MainWindow::onOpenSaveDir()
{
    QDir().mkpath(m_settings.saveDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.saveDir));
}

void MainWindow::onSendTextClicked()
{
    const QString text = ui->leMessage->text().trimmed();
    if (text.isEmpty())
        return;
    m_client->sendText(text);
    ui->leMessage->clear();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, QStringLiteral("关于"),
        QStringLiteral("Qt 文件传输工具 —— 课程作业\n\n"
                       "基于 QTcpSocket / QTcpServer 的 TCP 文件传输，\n"
                       "自定义协议头分包组包（防粘包），bytesWritten 流水线发送，\n"
                       "拖拽发送、QSS 美化、资源系统图标、跨窗口信号槽。\n\n"
                       "客户端与服务端集于同一程序：启动两个实例即可互传。"));
}

//---------------------------------------------------------------------
// 拖拽文件处理
//---------------------------------------------------------------------

void MainWindow::onFileDropped(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.isFile()) {
        appendLog(QStringLiteral("忽略非文件拖入：%1").arg(filePath), LogLevel::Warn);
        return;
    }
    if (m_connected) {
        // 客户端角色：拖入即发送
        m_client->sendFile(filePath);
    } else {
        // 服务端/未连接角色：拖入即保存到接收目录
        saveLocalFile(filePath);
    }
}

/* 未连接时把拖入的文件复制到接收目录（同名自动加序号） */
void MainWindow::saveLocalFile(const QString &filePath)
{
    QDir().mkpath(m_settings.saveDir);
    const QFileInfo info(filePath);
    QString target = QDir(m_settings.saveDir).filePath(info.fileName());
    if (QFileInfo::exists(target)) {
        const QString stem = info.completeBaseName();
        const QString suffix = info.suffix();
        for (int i = 1;; ++i) {
            const QString name = suffix.isEmpty()
                    ? QStringLiteral("%1(%2)").arg(stem).arg(i)
                    : QStringLiteral("%1(%2).%3").arg(stem).arg(i).arg(suffix);
            const QString cand = QDir(m_settings.saveDir).filePath(name);
            if (!QFileInfo::exists(cand)) { target = cand; break; }
        }
    }
    if (QFile::copy(filePath, target)) {
        appendLog(QStringLiteral("已保存到接收目录：%1").arg(target), LogLevel::Ok);
        statusBar()->showMessage(QStringLiteral("文件已保存到 %1").arg(target), 5000);
    } else {
        appendLog(QStringLiteral("保存失败：%1 -> %2").arg(filePath, target), LogLevel::Error);
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("无法把文件复制到接收目录，详见日志。"));
    }
}

//---------------------------------------------------------------------
// 服务端状态与进度显示
//---------------------------------------------------------------------

void MainWindow::onServerStateChanged(bool listening)
{
    m_listening = listening;
    updateUiState();
    updateTitle();
}

void MainWindow::onClientConnected(const QString &peer)
{
    m_peers.append(peer);
    updateUiState();
}

void MainWindow::onClientDisconnected(const QString &peer)
{
    m_peers.removeAll(peer);
    updateUiState();
}

void MainWindow::onRecvStarted(const QString &fileName, qint64 total)
{
    ui->progressBar->setValue(0);
    ui->lblTransfer->setText(QStringLiteral("正在接收：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("0 / %1（0%%）")
                                       .arg(Proto::formatFileSize(total)));
}

void MainWindow::onRecvProgress(const QString &fileName, qint64 received, qint64 total)
{
    const int percent = total > 0 ? int(received * 100 / total) : 100;
    ui->progressBar->setValue(percent);
    ui->lblTransfer->setText(QStringLiteral("正在接收：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("已接收 %1 / %2（%3%%）")
                                       .arg(Proto::formatFileSize(received),
                                            Proto::formatFileSize(total))
                                       .arg(percent));
}

void MainWindow::onRecvFinished(const QString &fileName, qint64 total, const QString &savedPath)
{
    ui->progressBar->setValue(100);
    ui->lblTransfer->setText(QStringLiteral("接收完成：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("共 %1，保存于 %2")
                                       .arg(Proto::formatFileSize(total), savedPath));
    statusBar()->showMessage(QStringLiteral("文件已保存：%1").arg(savedPath), 8000);
}

void MainWindow::onRecvFailed(const QString &reason)
{
    ui->lblTransfer->setText(QStringLiteral("接收失败：%1").arg(reason));
    statusBar()->showMessage(QStringLiteral("接收失败：%1").arg(reason), 8000);
}

void MainWindow::onTextReceived(const QString &peer, const QString &text)
{
    appendLog(QStringLiteral("收到来自 %1 的文本消息：%2").arg(peer, text), LogLevel::Ok);
}

//---------------------------------------------------------------------
// 客户端状态与进度显示
//---------------------------------------------------------------------

void MainWindow::onClientStateChanged(bool connected)
{
    m_connected = connected;
    updateUiState();
    updateTitle();
}

void MainWindow::onSendStarted(const QString &fileName, qint64 total)
{
    ui->progressBar->setValue(0);
    ui->lblTransfer->setText(QStringLiteral("正在发送：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("0 / %1（0%%）")
                                       .arg(Proto::formatFileSize(total)));
}

void MainWindow::onSendProgress(const QString &fileName, qint64 sent, qint64 total)
{
    const int percent = total > 0 ? int(sent * 100 / total) : 100;
    ui->progressBar->setValue(percent);
    ui->lblTransfer->setText(QStringLiteral("正在发送：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("已发送 %1 / %2（%3%%）")
                                       .arg(Proto::formatFileSize(sent),
                                            Proto::formatFileSize(total))
                                       .arg(percent));
}

void MainWindow::onSendFinished(const QString &fileName, qint64 total)
{
    ui->progressBar->setValue(100);
    ui->lblTransfer->setText(QStringLiteral("发送完成：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("共 %1").arg(Proto::formatFileSize(total)));
}

void MainWindow::onSendFailed(const QString &reason)
{
    ui->lblTransfer->setText(QStringLiteral("发送失败：%1").arg(reason));
    statusBar()->showMessage(QStringLiteral("发送失败：%1").arg(reason), 8000);
    QMessageBox::warning(this, QStringLiteral("发送失败"),
                         QStringLiteral("%1\n\n详细信息见日志。").arg(reason));
}

//---------------------------------------------------------------------
// 日志与界面状态
//---------------------------------------------------------------------

/* 向日志区追加一条带时间戳、按等级着色的记录 */
void MainWindow::appendLog(const QString &msg, LogLevel level)
{
    if (!ui)
        return;   // 界面已销毁（窗口关闭过程中），忽略残余信号
    QString color;
    switch (level) {
    case LogLevel::Ok:    color = QStringLiteral("#7ee787"); break;
    case LogLevel::Warn:  color = QStringLiteral("#e3b341"); break;
    case LogLevel::Error: color = QStringLiteral("#ff7b72"); break;
    default:              color = QStringLiteral("#c9d8ee"); break;
    }
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    ui->logView->append(QStringLiteral(
        "<span style='color:#6d7f99;'>[%1]</span> "
        "<span style='color:%2;'>%3</span>").arg(time, color, msg.toHtmlEscaped()));
}

void MainWindow::onLogInfo(const QString &msg)
{
    appendLog(msg, LogLevel::Info);
}

void MainWindow::onLogError(const QString &msg)
{
    appendLog(msg, LogLevel::Error);
}

/* 根据监听/连接状态刷新按钮文字、可用性与状态标签 */
void MainWindow::updateUiState()
{
    ui->btnListen->setText(m_listening ? QStringLiteral("停止监听")
                                       : QStringLiteral("启动监听"));
    ui->btnConnect->setText(m_connected ? QStringLiteral("断开连接")
                                        : QStringLiteral("连接服务器"));

    const bool canSend = m_connected;
    ui->btnOpenFile->setEnabled(canSend);
    ui->actOpenFile->setEnabled(canSend);
    ui->btnSendText->setEnabled(canSend);
    ui->leMessage->setEnabled(canSend);

    ui->lblConnState->setText(m_connected
        ? QStringLiteral("客户端：已连接 %1").arg(m_client->peerInfo())
        : QStringLiteral("客户端：未连接"));
    ui->lblServerState->setText(m_listening
        ? QStringLiteral("服务端：监听中 :%1（在线客户端 %2 个）")
              .arg(m_server->listenPort()).arg(m_peers.size())
        : QStringLiteral("服务端：未监听"));

    m_statusLabel->setText(QStringLiteral("监听：%1　连接：%2")
        .arg(m_listening ? QStringLiteral("开") : QStringLiteral("关"),
             m_connected ? QStringLiteral("已连接") : QStringLiteral("未连接")));
}

void MainWindow::updateTitle()
{
    QString title = QStringLiteral("Qt 文件传输工具");
    if (m_listening)
        title += QStringLiteral("　[监听中 :%1]").arg(m_server->listenPort());
    if (m_connected)
        title += QStringLiteral("　[已连接 %1]").arg(m_client->peerInfo());
    setWindowTitle(title);
}
