#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

// 应用运行角色：决定打开服务端窗口还是客户端窗口
enum class AppRole { Server, Client };

// 应用设置（对话框 <-> 主窗口之间传递的参数包）
struct AppSettings {
    QString host;          // 服务器 IP / 主机名（客户端使用）
    quint16 port = 8888;   // 监听（服务端）/ 连接（客户端）端口
    QString saveDir;       // 接收文件保存目录（服务端使用）
};

Q_DECLARE_METATYPE(AppSettings)   // 如需放入 QVariant / 队列连接时使用

namespace Ui { class SettingsDialog; }

/**
 * @brief 连接设置对话框，确定后通过 settingsApplied 信号把参数传给主窗口。
 * 按角色只显示相关字段：服务端=端口+保存目录，客户端=IP+端口。
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void setRole(AppRole role);               // 按角色显示/隐藏字段（须在 exec 前调用）
    void setSettings(const AppSettings &s);
    AppSettings settings() const;

signals:
    void settingsApplied(const AppSettings &settings);   // 参数确认 -> 发给主窗口

protected:
    // 事件过滤器：获得焦点时自动全选文字
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onBrowseClicked();   // "浏览..."选择保存目录
    void onOkClicked();       // "确定"：校验 -> 发信号 -> 关闭

private:
    Ui::SettingsDialog *ui;
    AppRole m_role = AppRole::Client;
};

#endif // SETTINGSDIALOG_H
