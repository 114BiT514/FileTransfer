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
 * @brief 客户端窗口：连接服务器、发送文本与文件（网络逻辑在 FileClient 中）。
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
    void openSettings();
    void onToggleConnect();
    void onOpenFileToSend();
    void onSendTextClicked();
    void onAbout();
    void onSettingsApplied(const AppSettings &settings);  // 设置对话框 -> 本窗口
    void onFileDropped(const QString &filePath);          // 拖入文件 -> 发送
    void onClientStateChanged(bool connected);
    void onSendStarted(const QString &fileName, qint64 total);
    void onSendProgress(const QString &fileName, qint64 sent, qint64 total);
    void onSendFinished(const QString &fileName, qint64 total);
    void onSendFailed(const QString &reason);
    void onLogInfo(const QString &msg);
    void onLogError(const QString &msg);

private slots:
    void resetProgressToIdle();  // 传输结束 1.5 秒后，进度区回到空闲的 0% 状态

private:
    void appendLog(const QString &msg, LogLevel level = LogLevel::Info);
    void updateUiState();
    void updateTitle();
    void loadSettings();
    void saveSettingsToDisk();
    static QIcon iconRes(const QString &name);

    Ui::ClientWindow *ui = nullptr;
    FileClient *m_client = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    QTimer *m_progressTimer = nullptr;  // 传输结束后延时复位进度区
    AppSettings m_settings;
    bool m_connected = false;
    QLabel *m_statusLabel = nullptr;
};

#endif // CLIENTWINDOW_H
