#include "clientwindow.h"
#include "ui_clientwindow.h"

#include "droparea.h"
#include "fileclient.h"
#include "protocol.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>

QIcon ClientWindow::iconRes(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.png").arg(name));
}

ClientWindow::ClientWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ClientWindow)
{
    ui->setupUi(this);

    // 布局伸缩因子：底部（日志+状态）区域随窗口拉伸
    ui->mainLayout->setStretch(3, 1);
    ui->bottomLayout->setStretch(0, 3);
    ui->bottomLayout->setStretch(1, 1);

    // Qt 资源系统图标
    ui->btnSettings->setIcon(iconRes(QStringLiteral("settings")));
    ui->btnConnect->setIcon(iconRes(QStringLiteral("connect")));
    ui->btnOpenFile->setIcon(iconRes(QStringLiteral("send")));
    ui->btnSendText->setIcon(iconRes(QStringLiteral("send")));
    ui->btnExit->setIcon(iconRes(QStringLiteral("exit")));
    ui->actOpenFile->setIcon(iconRes(QStringLiteral("send")));
    ui->actExit->setIcon(iconRes(QStringLiteral("exit")));
    ui->actSettings->setIcon(iconRes(QStringLiteral("settings")));
    ui->actAbout->setIcon(iconRes(QStringLiteral("app")));

    // 客户端窗口的拖拽提示语
    ui->dropArea->setHint(QStringLiteral("将文件拖拽到此处\n松开鼠标即发送给服务器"));

    // 发送逻辑（Model 层）
    m_client = new FileClient(this);

    m_statusLabel = new QLabel(QStringLiteral("未连接"), this);
    ui->statusbar->addPermanentWidget(m_statusLabel);

    loadSettings();

    /* ==================== 信号与槽连接 ==================== */
    // 1) 按钮与输入框
    connect(ui->btnSettings, &QPushButton::clicked, this, &ClientWindow::openSettings);
    connect(ui->btnConnect, &QPushButton::clicked, this, &ClientWindow::onToggleConnect);
    connect(ui->btnOpenFile, &QPushButton::clicked, this, &ClientWindow::onOpenFileToSend);
    connect(ui->btnExit, &QPushButton::clicked, this, &QWidget::close);
    connect(ui->btnSendText, &QPushButton::clicked, this, &ClientWindow::onSendTextClicked);
    connect(ui->leMessage, &QLineEdit::returnPressed, this, &ClientWindow::onSendTextClicked);
    // 2) 菜单
    connect(ui->actOpenFile, &QAction::triggered, this, &ClientWindow::onOpenFileToSend);
    connect(ui->actExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actSettings, &QAction::triggered, this, &ClientWindow::openSettings);
    connect(ui->actAbout, &QAction::triggered, this, &ClientWindow::onAbout);
    // 3) FileClient 信号 -> 界面
    connect(m_client, &FileClient::stateChanged, this, &ClientWindow::onClientStateChanged);
    connect(m_client, &FileClient::sendStarted,  this, &ClientWindow::onSendStarted);
    connect(m_client, &FileClient::sendProgress, this, &ClientWindow::onSendProgress);
    connect(m_client, &FileClient::sendFinished, this, &ClientWindow::onSendFinished);
    connect(m_client, &FileClient::sendFailed,   this, &ClientWindow::onSendFailed);
    connect(m_client, &FileClient::logInfo,      this, &ClientWindow::onLogInfo);
    connect(m_client, &FileClient::logError,     this, &ClientWindow::onLogError);
    // 4) 拖拽区：拖入文件 -> 发送
    connect(ui->dropArea, &DropArea::fileDropped, this, &ClientWindow::onFileDropped);

    updateUiState();
    updateTitle();

    appendLog(QStringLiteral("本窗口是【客户端】：先在\"连接设置\"里填写服务端 IP/端口，"
                            "再点击\"连接服务器\"。"), LogLevel::Ok);
    appendLog(QStringLiteral("连接成功后，把文件拖进虚线框或点\"选择文件发送\"即可发送。"), LogLevel::Info);
}

ClientWindow::~ClientWindow()
{
    // 析构顺序：先断开网络并切断信号，再销毁界面（防止残余信号访问已释放控件）
    m_client->disconnectFromServer();
    m_client->disconnect(this);
    delete ui;
    ui = nullptr;
}

//---------------------------------------------------------------------
// 设置
//---------------------------------------------------------------------

void ClientWindow::loadSettings()
{
    QSettings s;
    m_settings.host = s.value(QStringLiteral("network/host"),
                              QStringLiteral("127.0.0.1")).toString();
    m_settings.port = quint16(s.value(QStringLiteral("network/port"), 8888).toUInt());
}

void ClientWindow::saveSettingsToDisk()
{
    QSettings s;
    s.setValue(QStringLiteral("network/host"), m_settings.host);
    s.setValue(QStringLiteral("network/port"), m_settings.port);
}

/* 打开"连接设置"对话框（跨窗口信号槽把参数传回） */
void ClientWindow::openSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
        // 【跨窗口通信】对话框 -> 本窗口
        connect(m_settingsDialog, &SettingsDialog::settingsApplied,
                this, &ClientWindow::onSettingsApplied);
    }
    m_settingsDialog->setRole(AppRole::Client);   // 对话框按角色只显示 IP/端口
    m_settingsDialog->setSettings(m_settings);
    m_settingsDialog->exec();
}

void ClientWindow::onSettingsApplied(const AppSettings &settings)
{
    const bool changed = (m_settings.host != settings.host || m_settings.port != settings.port);
    m_settings.host = settings.host;
    m_settings.port = settings.port;
    saveSettingsToDisk();
    updateUiState();
    appendLog(QStringLiteral("连接设置已更新：目标服务器 %1:%2")
                  .arg(m_settings.host).arg(m_settings.port), LogLevel::Ok);
    if (changed && m_connected)
        appendLog(QStringLiteral("提示：目标已修改，断开后重新连接才会生效。"), LogLevel::Warn);
}

//---------------------------------------------------------------------
// 界面动作
//---------------------------------------------------------------------

void ClientWindow::onToggleConnect()
{
    if (m_connected) {
        m_client->disconnectFromServer();       // 已连接 -> 断开
        return;
    }
    if (m_client->socketState() != QAbstractSocket::UnconnectedState)
        return;                                 // 正在连接中，忽略重复点击
    if (m_settings.host.trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先在\"连接设置\"中填写服务器 IP。"));
        return;
    }
    m_client->connectToServer(m_settings.host, m_settings.port);
}

void ClientWindow::onOpenFileToSend()
{
    const QString filter = QStringLiteral(
        "文本文件 (*.txt *.log *.csv);;图片文件 (*.png *.jpg *.jpeg *.bmp);;所有文件 (*)");
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择要发送的文件"), QString(), filter);
    if (path.isEmpty())
        return;
    m_client->sendFile(path);
}

void ClientWindow::onSendTextClicked()
{
    const QString text = ui->leMessage->text().trimmed();
    if (text.isEmpty())
        return;
    m_client->sendText(text);
    ui->leMessage->clear();
}

void ClientWindow::onAbout()
{
    QMessageBox::about(this, QStringLiteral("关于"),
        QStringLiteral("Qt 文件传输工具 —— 客户端\n\n"
                       "基于 QTcpSocket 的文件发送端：自定义协议头 + bytesWritten 流水线发送，\n"
                       "支持文本消息与任意文件（文本/图片等），大文件不占用额外内存。\n\n"
                       "与服务端窗口配套使用：先连接服务端 IP 与监听端口。"));
}

/* 拖入文件：客户端角色 = 发送给服务器 */
void ClientWindow::onFileDropped(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.isFile()) {
        appendLog(QStringLiteral("忽略非文件拖入：%1").arg(filePath), LogLevel::Warn);
        return;
    }
    if (!m_connected) {
        appendLog(QStringLiteral("尚未连接服务器，请先点击\"连接服务器\"。"), LogLevel::Warn);
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("尚未连接服务器，无法发送。请先连接服务器。"));
        return;
    }
    appendLog(QStringLiteral("检测到拖入文件，开始发送：%1").arg(info.fileName()), LogLevel::Info);
    m_client->sendFile(filePath);
}

//---------------------------------------------------------------------
// FileClient 信号 -> 界面刷新
//---------------------------------------------------------------------

void ClientWindow::onClientStateChanged(bool connected)
{
    m_connected = connected;
    updateUiState();
    updateTitle();
}

void ClientWindow::onSendStarted(const QString &fileName, qint64 total)
{
    ui->progressBar->setValue(0);
    ui->lblTransfer->setText(QStringLiteral("正在发送：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("0 / %1（0%%）")
                                       .arg(Proto::formatFileSize(total)));
}

void ClientWindow::onSendProgress(const QString &fileName, qint64 sent, qint64 total)
{
    const int percent = total > 0 ? int(sent * 100 / total) : 100;
    ui->progressBar->setValue(percent);
    ui->lblTransfer->setText(QStringLiteral("正在发送：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("已发送 %1 / %2（%3%%）")
                                       .arg(Proto::formatFileSize(sent),
                                            Proto::formatFileSize(total))
                                       .arg(percent));
}

void ClientWindow::onSendFinished(const QString &fileName, qint64 total)
{
    ui->progressBar->setValue(100);
    ui->lblTransfer->setText(QStringLiteral("发送完成：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("共 %1").arg(Proto::formatFileSize(total)));
    statusBar()->showMessage(QStringLiteral("发送完成：%1").arg(fileName), 8000);
}

void ClientWindow::onSendFailed(const QString &reason)
{
    ui->lblTransfer->setText(QStringLiteral("发送失败：%1").arg(reason));
    statusBar()->showMessage(QStringLiteral("发送失败：%1").arg(reason), 8000);
    QMessageBox::warning(this, QStringLiteral("发送失败"),
                         QStringLiteral("%1\n\n详细信息见日志。").arg(reason));
}

//---------------------------------------------------------------------
// 日志与界面状态
//---------------------------------------------------------------------

void ClientWindow::appendLog(const QString &msg, LogLevel level)
{
    if (!ui)
        return;
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

void ClientWindow::onLogInfo(const QString &msg)
{
    appendLog(msg, LogLevel::Info);
}

void ClientWindow::onLogError(const QString &msg)
{
    appendLog(msg, LogLevel::Error);
}

void ClientWindow::updateUiState()
{
    ui->btnConnect->setText(m_connected ? QStringLiteral("断开连接")
                                        : QStringLiteral("连接服务器"));
    const bool canSend = m_connected;
    ui->btnOpenFile->setEnabled(canSend);
    ui->actOpenFile->setEnabled(canSend);
    ui->btnSendText->setEnabled(canSend);
    ui->leMessage->setEnabled(canSend);

    ui->lblConnState->setText(m_connected
        ? QStringLiteral("连接状态：已连接 %1").arg(m_client->peerInfo())
        : QStringLiteral("连接状态：未连接"));
    ui->lblTarget->setText(QStringLiteral("目标服务器：%1:%2")
                               .arg(m_settings.host).arg(m_settings.port));
    m_statusLabel->setText(m_connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));
}

void ClientWindow::updateTitle()
{
    QString title = QStringLiteral("Qt 文件传输工具 - 客户端");
    if (m_connected)
        title += QStringLiteral("　[已连接 %1]").arg(m_client->peerInfo());
    setWindowTitle(title);
}
