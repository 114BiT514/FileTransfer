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
class QTimer;

/**
 * @brief 服务端窗口：监听端口、接收并保存文件（网络逻辑在 FileServer 中）。
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
    void openSettings();
    void onToggleListen();
    void onOpenSaveDir();
    void onAbout();
    void onSettingsApplied(const AppSettings &settings);  // 设置对话框 -> 本窗口
    void onFileDropped(const QString &filePath);          // 拖入文件 -> 保存
    void onServerStateChanged(bool listening);
    void onClientConnected(const QString &peer);
    void onClientDisconnected(const QString &peer);
    void onRecvStarted(const QString &fileName, qint64 total);
    void onRecvProgress(const QString &fileName, qint64 received, qint64 total);
    void onRecvFinished(const QString &fileName, qint64 total, const QString &savedPath);
    void onRecvFailed(const QString &reason);
    void onTextReceived(const QString &peer, const QString &text);
    void onLogInfo(const QString &msg);
    void onLogError(const QString &msg);

private slots:
    void resetProgressToIdle();  // 传输结束 1.5 秒后，进度区回到空闲的 0% 状态

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
    FileServer *m_server = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    QTimer *m_progressTimer = nullptr;  // 传输结束后延时复位进度区
    AppSettings m_settings;
    bool m_listening = false;
    QStringList m_peers;
    QLabel *m_statusLabel = nullptr;
};

#endif // SERVERWINDOW_H
