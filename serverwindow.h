#ifndef SERVERWINDOW_H
#define SERVERWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QStringList>
#include "settingsdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ServerWindow; }
QT_END_NAMESPACE

class FileServer;

/**
 * @brief 服务端窗口（View 层）：只负责"监听端口、接收并保存文件"，
 *        不包含任何发送功能；网络逻辑全部在 FileServer（Model 层）中。
 *
 * 界面组成：监听/设置按钮行 + 拖拽接收区（拖入即保存）+ 进度条 + 日志 + 状态栏。
 */
class ServerWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit ServerWindow(QWidget *parent = nullptr);
    ~ServerWindow() override;

private:
    enum class LogLevel { Info, Ok, Warn, Error };

private slots:
    // ---- 界面动作 ----
    void openSettings();            // 打开"服务端设置"（监听端口 + 保存目录）
    void onToggleListen();          // 启动 / 停止监听
    void onOpenSaveDir();           // 打开接收保存目录
    void onAbout();                 // 关于
    // ---- 跨窗口通信：设置对话框 -> 本窗口 ----
    void onSettingsApplied(const AppSettings &settings);
    // ---- 拖拽区 -> 本窗口（服务端角色：保存）----
    void onFileDropped(const QString &filePath);
    // ---- FileServer 信号 -> 界面刷新 ----
    void onServerStateChanged(bool listening);
    void onClientConnected(const QString &peer);
    void onClientDisconnected(const QString &peer);
    void onRecvStarted(const QString &fileName, qint64 total);
    void onRecvProgress(const QString &fileName, qint64 received, qint64 total);
    void onRecvFinished(const QString &fileName, qint64 total, const QString &savedPath);
    void onRecvFailed(const QString &reason);
    void onTextReceived(const QString &peer, const QString &text);
    // ---- 日志 ----
    void onLogInfo(const QString &msg);
    void onLogError(const QString &msg);

private:
    void appendLog(const QString &msg, LogLevel level = LogLevel::Info);
    void updateUiState();
    void updateTitle();
    void applySettingsToUi();
    void loadSettings();
    void saveSettingsToDisk();
    void saveLocalFile(const QString &filePath);   // 拖入文件 -> 保存到接收目录
    static QIcon iconRes(const QString &name);

    Ui::ServerWindow *ui = nullptr;
    FileServer *m_server = nullptr;               // 接收逻辑（Model 层）
    SettingsDialog *m_settingsDialog = nullptr;
    AppSettings m_settings;                       // 使用 port 与 saveDir
    bool m_listening = false;
    QStringList m_peers;                          // 在线客户端列表
    QLabel *m_statusLabel = nullptr;              // 状态栏常驻信息
};

#endif // SERVERWINDOW_H
