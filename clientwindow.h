#ifndef CLIENTWINDOW_H
#define CLIENTWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include "settingsdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ClientWindow; }
QT_END_NAMESPACE

class FileClient;
class QTimer;

/**
 * @brief 客户端窗口（View 层）：只负责"连接服务器、发送文本/文件"，
 *        不包含任何监听/保存功能；网络逻辑全部在 FileClient（Model 层）中。
 *
 * 界面组成：连接/发送按钮行 + 拖拽发送区（拖入即发送）+ 进度条 + 日志 + 文本消息。
 */
class ClientWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit ClientWindow(QWidget *parent = nullptr);
    ~ClientWindow() override;

private:
    enum class LogLevel { Info, Ok, Warn, Error };

private slots:
    // ---- 界面动作 ----
    void openSettings();            // 打开"连接设置"（服务器 IP + 端口）
    void onToggleConnect();         // 连接 / 断开服务器
    void onOpenFileToSend();        // 选择文件并发送
    void onSendTextClicked();       // 发送文本消息
    void onAbout();                 // 关于
    // ---- 跨窗口通信：设置对话框 -> 本窗口 ----
    void onSettingsApplied(const AppSettings &settings);
    // ---- 拖拽区 -> 本窗口（客户端角色：发送）----
    void onFileDropped(const QString &filePath);
    // ---- FileClient 信号 -> 界面刷新 ----
    void onClientStateChanged(bool connected);
    void onSendStarted(const QString &fileName, qint64 total);
    void onSendProgress(const QString &fileName, qint64 sent, qint64 total);
    void onSendFinished(const QString &fileName, qint64 total);
    void onSendFailed(const QString &reason);
    // ---- 日志 ----
    void onLogInfo(const QString &msg);
    void onLogError(const QString &msg);

private slots:
    void resetProgressToIdle();  // 传输结束保留数秒后，进度区回到空闲的 0% 状态

private:
    void appendLog(const QString &msg, LogLevel level = LogLevel::Info);
    void updateUiState();
    void updateTitle();
    void loadSettings();
    void saveSettingsToDisk();
    static QIcon iconRes(const QString &name);

    Ui::ClientWindow *ui = nullptr;
    FileClient *m_client = nullptr;               // 发送逻辑（Model 层）
    SettingsDialog *m_settingsDialog = nullptr;
    QTimer *m_progressTimer = nullptr;            // 完成后延时隐藏进度区
    AppSettings m_settings;                       // 使用 host 与 port
    bool m_connected = false;
    QLabel *m_statusLabel = nullptr;              // 状态栏常驻信息
};

#endif // CLIENTWINDOW_H
