#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

// 应用设置（对话框 <-> 主窗口之间传递的参数包）
struct AppSettings {
    QString host;          // 服务器 IP / 主机名
    quint16 port = 8888;   // 监听 / 连接端口
    QString saveDir;       // 接收文件保存目录
};

Q_DECLARE_METATYPE(AppSettings)   // 如需放入 QVariant / 队列连接时使用

namespace Ui { class SettingsDialog; }

/**
 * @brief "连接设置"对话框（必做项：窗口间通信）
 *
 * 用于设置服务器 IP、端口、接收保存目录。
 * 点击"确定"后通过自定义信号 settingsApplied(AppSettings)
 * 把参数传递给主窗口 —— 考察信号与槽的跨窗口通信。
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void setSettings(const AppSettings &s);   // 打开对话框前填入当前设置
    AppSettings settings() const;             // 读取对话框中的设置

signals:
    void settingsApplied(const AppSettings &settings);   // 参数确认 -> 发给主窗口

protected:
    // 【事件过滤器】安装到输入控件上：获得焦点时自动全选文字
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onBrowseClicked();   // "浏览..."选择保存目录
    void onOkClicked();       // "确定"：校验 -> 发信号 -> 关闭

private:
    Ui::SettingsDialog *ui;
};

#endif // SETTINGSDIALOG_H
