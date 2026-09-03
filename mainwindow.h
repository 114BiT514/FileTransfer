#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QStringList>
#include "settingsdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class FileServer;
class FileClient;

/**
 * @brief 主窗口（View 层）：只负责界面显示与用户交互，
 *        所有网络与文件读写逻辑都在 FileServer / FileClient（Model 层）中。
 *
 * 窗口结构：工具按钮行 + 拖拽接收区 + 进度条 + 日志/状态区 + 菜单 + 状态栏
 * 通信关系：
 *   - SettingsDialog --settingsApplied(AppSettings)--> MainWindow（跨窗口信号槽）
 *   - DropArea --fileDropped(path)--> MainWindow
 *   - FileServer / FileClient --log/progress/state 信号--> MainWindow 刷新显示
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    enum class LogLevel { Info, Ok, Warn, Error };   // 日志着色等级

private slots:
    // ---- 界面动作 ----
    void openSettings();          // 打开"连接设置"对话框
    void onToggleListen();        // 启动 / 停止监听
    void onToggleConnect();       // 连接 / 断开服务器
    void onOpenFileToSend();      // 打开文件选择对话框并发送
    void onOpenSaveDir();         // 打开接收保存目录
    void onSendTextClicked();     // 发送文本消息（测试连通性）
    void onAbout();               // 关于

    // ---- 跨窗口通信：设置对话框 -> 主窗口 ----
    void onSettingsApplied(const AppSettings &settings);

    // ---- 拖拽区 -> 主窗口 ----
    void onFileDropped(const QString &filePath);

    // ---- FileServer（服务端）信号 -> 界面刷新 ----
    void onServerStateChanged(bool listening);
    void onClientConnected(const QString &peer);
    void onClientDisconnected(const QString &peer);
    void onRecvStarted(const QString &fileName, qint64 total);
    void onRecvProgress(const QString &fileName, qint64 received, qint64 total);
    void onRecvFinished(const QString &fileName, qint64 total, const QString &savedPath);
    void onRecvFailed(const QString &reason);
    void onTextReceived(const QString &peer, const QString &text);

    // ---- FileClient（客户端）信号 -> 界面刷新 ----
    void onClientStateChanged(bool connected);
    void onSendStarted(const QString &fileName, qint64 total);
    void onSendProgress(const QString &fileName, qint64 sent, qint64 total);
    void onSendFinished(const QString &fileName, qint64 total);
    void onSendFailed(const QString &reason);

    // ---- 日志 ----
    void onLogInfo(const QString &msg);
    void onLogError(const QString &msg);

private:
    void appendLog(const QString &msg, LogLevel level = LogLevel::Info);
    void updateUiState();          // 根据连接/监听状态刷新按钮与标签
    void updateTitle();            // 根据状态刷新窗口标题
    void applySettingsToUi();      // 把设置同步到界面显示
    void loadSettings();           // 从 QSettings 读取上次保存的设置
    void saveSettingsToDisk();     // 设置持久化
    void saveLocalFile(const QString &filePath);  // 未连接时：把拖入文件保存到接收目录
    static QIcon iconRes(const QString &name);    // 从资源系统取图标 ":/icons/xxx.png"

    Ui::MainWindow *ui = nullptr;
    FileServer *m_server = nullptr;          // 服务端（接收）逻辑
    FileClient *m_client = nullptr;          // 客户端（发送）逻辑
    SettingsDialog *m_settingsDialog = nullptr;
    AppSettings m_settings;                  // 当前生效的设置
    bool m_listening = false;                // 服务端监听中
    bool m_connected = false;                // 客户端已连接
    QStringList m_peers;                     // 在线客户端列表
    QLabel *m_statusLabel = nullptr;         // 状态栏常驻信息
};

#endif // MAINWINDOW_H
