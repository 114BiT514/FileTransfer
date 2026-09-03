#include "serverwindow.h"
#include "ui_serverwindow.h"

#include "droparea.h"
#include "fileserver.h"
#include "protocol.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

namespace {
// 传输结束（100%或失败）后，进度区保留 1.5 秒，然后回到空闲的 0% 状态
constexpr int kProgressResetMs = 1500;
}

QIcon ServerWindow::iconRes(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.png").arg(name));
}

ServerWindow::ServerWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ServerWindow)
{
    ui->setupUi(this);

    // 布局伸缩因子：底部（日志+状态）区域随窗口拉伸
    ui->mainLayout->setStretch(3, 1);
    ui->bottomLayout->setStretch(0, 3);
    ui->bottomLayout->setStretch(1, 1);

    // Qt 资源系统图标
    ui->btnSettings->setIcon(iconRes(QStringLiteral("settings")));
    ui->btnListen->setIcon(iconRes(QStringLiteral("server")));
    ui->btnOpenSaveDir->setIcon(iconRes(QStringLiteral("folder")));
    ui->btnExit->setIcon(iconRes(QStringLiteral("exit")));
    ui->actOpenSaveDir->setIcon(iconRes(QStringLiteral("folder")));
    ui->actExit->setIcon(iconRes(QStringLiteral("exit")));
    ui->actSettings->setIcon(iconRes(QStringLiteral("settings")));
    ui->actAbout->setIcon(iconRes(QStringLiteral("app")));

    // 服务端窗口的拖拽提示语
    ui->dropArea->setHint(QStringLiteral("将文件拖拽到此处\n松开鼠标即保存到接收目录"));

    // 接收逻辑（Model 层）
    m_server = new FileServer(this);

    m_statusLabel = new QLabel(QStringLiteral("未监听"), this);
    ui->statusbar->addPermanentWidget(m_statusLabel);

    loadSettings();

    /* ==================== 信号与槽连接 ==================== */
    // 1) 按钮
    connect(ui->btnSettings, &QPushButton::clicked, this, &ServerWindow::openSettings);
    connect(ui->btnListen, &QPushButton::clicked, this, &ServerWindow::onToggleListen);
    connect(ui->btnOpenSaveDir, &QPushButton::clicked, this, &ServerWindow::onOpenSaveDir);
    connect(ui->btnExit, &QPushButton::clicked, this, &QWidget::close);
    // 2) 菜单
    connect(ui->actOpenSaveDir, &QAction::triggered, this, &ServerWindow::onOpenSaveDir);
    connect(ui->actExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actSettings, &QAction::triggered, this, &ServerWindow::openSettings);
    connect(ui->actAbout, &QAction::triggered, this, &ServerWindow::onAbout);
    // 3) FileServer 信号 -> 界面
    connect(m_server, &FileServer::stateChanged,       this, &ServerWindow::onServerStateChanged);
    connect(m_server, &FileServer::clientConnected,    this, &ServerWindow::onClientConnected);
    connect(m_server, &FileServer::clientDisconnected, this, &ServerWindow::onClientDisconnected);
    connect(m_server, &FileServer::recvStarted,        this, &ServerWindow::onRecvStarted);
    connect(m_server, &FileServer::recvProgress,       this, &ServerWindow::onRecvProgress);
    connect(m_server, &FileServer::recvFinished,       this, &ServerWindow::onRecvFinished);
    connect(m_server, &FileServer::recvFailed,         this, &ServerWindow::onRecvFailed);
    connect(m_server, &FileServer::textReceived,       this, &ServerWindow::onTextReceived);
    connect(m_server, &FileServer::logInfo,            this, &ServerWindow::onLogInfo);
    connect(m_server, &FileServer::logError,           this, &ServerWindow::onLogError);
    // 4) 拖拽区：拖入文件 -> 保存
    connect(ui->dropArea, &DropArea::fileDropped, this, &ServerWindow::onFileDropped);

    // 【信号与槽】传输结束后保留 1.5 秒，由定时器把进度区复位为空闲的 0% 状态
    // （完成/失败/本地保存三条结束路径都会启动它，进度条不会卡在任何数值上）
    m_progressTimer = new QTimer(this);
    m_progressTimer->setSingleShot(true);
    m_progressTimer->setInterval(kProgressResetMs);
    connect(m_progressTimer, &QTimer::timeout, this, &ServerWindow::resetProgressToIdle);

    applySettingsToUi();
    updateUiState();

    appendLog(QStringLiteral("本窗口是【服务端】：点击\"启动监听\"等待客户端连入。"), LogLevel::Ok);
    appendLog(QStringLiteral("收到文件后将保存到\"保存目录\"（可在\"服务端设置\"中修改），"
                            "把文件拖进虚线框也可直接保存到该目录。"), LogLevel::Info);
}

ServerWindow::~ServerWindow()
{
    // 析构顺序：先停止网络逻辑并切断其到本窗口的信号，再销毁界面，
    // 避免子对象析构过程中发出的日志/状态信号访问已释放的控件
    m_server->stopListen();
    m_server->disconnect(this);
    delete ui;
    ui = nullptr;
}

//---------------------------------------------------------------------
// 设置
//---------------------------------------------------------------------

void ServerWindow::loadSettings()
{
    QSettings s;
    m_settings.host = QStringLiteral("0.0.0.0");   // 服务端不使用该字段
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

void ServerWindow::saveSettingsToDisk()
{
    QSettings s;
    s.setValue(QStringLiteral("network/port"), m_settings.port);
    s.setValue(QStringLiteral("paths/saveDir"), m_settings.saveDir);
}

void ServerWindow::applySettingsToUi()
{
    ui->lblSaveDir->setText(QStringLiteral("保存目录：%1").arg(m_settings.saveDir));
    ui->lblSaveDir->setToolTip(m_settings.saveDir);
}

/* 打开"服务端设置"对话框（跨窗口信号槽把参数传回） */
void ServerWindow::openSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(this);
        // 【跨窗口通信】对话框 -> 本窗口
        connect(m_settingsDialog, &SettingsDialog::settingsApplied,
                this, &ServerWindow::onSettingsApplied);
    }
    m_settingsDialog->setRole(AppRole::Server);   // 对话框按角色只显示相关字段
    m_settingsDialog->setSettings(m_settings);
    m_settingsDialog->exec();
}

void ServerWindow::onSettingsApplied(const AppSettings &settings)
{
    const bool portChanged = (m_settings.port != settings.port);
    m_settings.port = settings.port;
    m_settings.saveDir = settings.saveDir;
    m_server->setSaveDir(m_settings.saveDir);     // 保存目录立即生效
    applySettingsToUi();
    saveSettingsToDisk();
    appendLog(QStringLiteral("服务端设置已更新：监听端口 %1，保存目录 %2")
                  .arg(m_settings.port).arg(m_settings.saveDir), LogLevel::Ok);
    if (portChanged && m_listening)
        appendLog(QStringLiteral("提示：监听端口已修改，请先停止监听再重新启动以生效。"),
                  LogLevel::Warn);
}

//---------------------------------------------------------------------
// 界面动作
//---------------------------------------------------------------------

void ServerWindow::onToggleListen()
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

void ServerWindow::onOpenSaveDir()
{
    QDir().mkpath(m_settings.saveDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.saveDir));
}

void ServerWindow::onAbout()
{
    QMessageBox::about(this, QStringLiteral("关于"),
        QStringLiteral("Qt 文件传输工具 —— 服务端\n\n"
                       "基于 QTcpServer 的文件接收端：自定义协议分包组包（防粘包），\n"
                       "收到的文件先写 .part 临时文件，完成后改名保存，同名自动加序号。\n\n"
                       "与客户端窗口配套使用：客户端连接本机 IP 与监听端口即可发送文件。\n\n"
                       "构建标记：进度自动归零版 v3"));
}

/* 拖入文件：服务端角色 = 保存到接收目录 */
void ServerWindow::onFileDropped(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.isFile()) {
        appendLog(QStringLiteral("忽略非文件拖入：%1").arg(filePath), LogLevel::Warn);
        return;
    }
    saveLocalFile(filePath);
}

/* 把拖入的文件复制到接收目录（同名自动加序号） */
void ServerWindow::saveLocalFile(const QString &filePath)
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
        // 本地拖入保存也用进度区给出直观反馈：显示 100%，保留 1.5 秒后回到空闲状态
        m_progressTimer->stop();
        ui->progressBar->setValue(100);
        ui->lblTransfer->setText(QStringLiteral("已保存：%1").arg(info.fileName()));
        ui->lblProgressDetail->setText(target);
        m_progressTimer->start();
        appendLog(QStringLiteral("已保存到接收目录：%1").arg(target), LogLevel::Ok);
        statusBar()->showMessage(QStringLiteral("文件已保存到 %1").arg(target), 5000);
    } else {
        appendLog(QStringLiteral("保存失败：%1 -> %2").arg(filePath, target), LogLevel::Error);
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("无法把文件复制到接收目录，详见日志。"));
    }
}

//---------------------------------------------------------------------
// FileServer 信号 -> 界面刷新
//---------------------------------------------------------------------

void ServerWindow::onServerStateChanged(bool listening)
{
    m_listening = listening;
    updateUiState();
    updateTitle();
}

void ServerWindow::onClientConnected(const QString &peer)
{
    m_peers.append(peer);
    updateUiState();
}

void ServerWindow::onClientDisconnected(const QString &peer)
{
    m_peers.removeAll(peer);
    updateUiState();
}

void ServerWindow::onRecvStarted(const QString &fileName, qint64 total)
{
    m_progressTimer->stop();         // 新任务开始：取消尚未触发的复位定时
    ui->progressBar->setValue(0);
    ui->lblTransfer->setText(QStringLiteral("正在接收：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("0 / %1（0%%）")
                                       .arg(Proto::formatFileSize(total)));
}

void ServerWindow::onRecvProgress(const QString &fileName, qint64 received, qint64 total)
{
    const int percent = total > 0 ? int(received * 100 / total) : 100;
    ui->progressBar->setValue(percent);
    ui->lblTransfer->setText(QStringLiteral("正在接收：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("已接收 %1 / %2（%3%%）")
                                       .arg(Proto::formatFileSize(received),
                                            Proto::formatFileSize(total))
                                       .arg(percent));
    if (received >= total) {
        // 双保险：进度已到 100% 就启动复位定时。正常情况下 recvFinished 会先来
        // 并启动同一个定时（重复 start 只是重新计时），这里保证即便结束信号
        // 因任何原因没有送达，进度条也不会永远停在 100%。
        m_progressTimer->start();
    }
}

void ServerWindow::onRecvFinished(const QString &fileName, qint64 total, const QString &savedPath)
{
    ui->progressBar->setValue(100);
    ui->lblTransfer->setText(QStringLiteral("接收完成：%1").arg(fileName));
    ui->lblProgressDetail->setText(QStringLiteral("共 %1，保存于 %2")
                                       .arg(Proto::formatFileSize(total), savedPath));
    statusBar()->showMessage(QStringLiteral("文件已保存：%1").arg(savedPath), 8000);
    m_progressTimer->start();        // 100% 完成信息保留 1.5 秒后回到空闲状态
}

void ServerWindow::onRecvFailed(const QString &reason)
{
    // 失败原因短暂显示后同样复位（详细原因已写入日志），避免进度条卡在中途数值
    m_progressTimer->start();
    ui->lblTransfer->setText(QStringLiteral("接收失败：%1").arg(reason));
    statusBar()->showMessage(QStringLiteral("接收失败：%1").arg(reason), 8000);
}

/* 定时器到点：回到最初的空闲状态 —— 0% 灰色进度条 + "当前没有接收任务" */
void ServerWindow::resetProgressToIdle()
{
    ui->progressBar->setValue(0);
    ui->lblTransfer->setText(QStringLiteral("当前没有接收任务"));
    ui->lblProgressDetail->setText(QString());
}

void ServerWindow::onTextReceived(const QString &peer, const QString &text)
{
    appendLog(QStringLiteral("收到来自 %1 的文本消息：%2").arg(peer, text), LogLevel::Ok);
}

//---------------------------------------------------------------------
// 日志与界面状态
//---------------------------------------------------------------------

void ServerWindow::appendLog(const QString &msg, LogLevel level)
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

void ServerWindow::onLogInfo(const QString &msg)
{
    appendLog(msg, LogLevel::Info);
}

void ServerWindow::onLogError(const QString &msg)
{
    appendLog(msg, LogLevel::Error);
}

void ServerWindow::updateUiState()
{
    ui->btnListen->setText(m_listening ? QStringLiteral("停止监听")
                                       : QStringLiteral("启动监听"));
    ui->lblServerState->setText(m_listening
        ? QStringLiteral("服务端：监听中 :%1").arg(m_server->listenPort())
        : QStringLiteral("服务端：未监听"));
    ui->lblClientCount->setText(QStringLiteral("在线客户端：%1 个").arg(m_peers.size()));
    m_statusLabel->setText(m_listening ? QStringLiteral("监听中") : QStringLiteral("未监听"));
}

void ServerWindow::updateTitle()
{
    QString title = QStringLiteral("Qt 文件传输工具 - 服务端");
    if (m_listening)
        title += QStringLiteral("　[监听中 :%1]").arg(m_server->listenPort());
    setWindowTitle(title);
}
