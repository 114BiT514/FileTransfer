#ifndef DROPAREA_H
#define DROPAREA_H

#include <QFrame>
#include <QLabel>

/**
 * @brief 拖拽接收区（自定义控件，体现 QWidget 虚函数重写）
 *
 * 把文件拖进该区域时给出高亮反馈（配合 QSS 动态属性 dragOver），
 * 松开鼠标（dropEvent）后发出 fileDropped 信号：
 *   - 客户端已连接 -> 主窗口把文件发送出去；
 *   - 未连接/服务端 -> 主窗口把文件保存到接收目录。
 *
 * 重写的虚函数：
 *   dragEnterEvent / dragMoveEvent / dragLeaveEvent / dropEvent
 */
class DropArea : public QFrame
{
    Q_OBJECT
public:
    explicit DropArea(QWidget *parent = nullptr);

signals:
    void fileDropped(const QString &filePath);   // 每个被拖入的本地文件发一次

protected:
    // ---- QWidget 提供的拖拽事件虚函数，此处重写实现自定义交互 ----
    void dragEnterEvent(QDragEnterEvent *event) override;  // 文件拖入窗口边界
    void dragMoveEvent(QDragMoveEvent *event) override;    // 文件在区域内移动
    void dragLeaveEvent(QDragLeaveEvent *event) override;  // 文件拖离区域/取消
    void dropEvent(QDropEvent *event) override;            // 松开鼠标放下

private:
    void setDragOver(bool on);   // 切换高亮状态（动态属性 + 重新应用 QSS）

    QLabel *m_icon = nullptr;    // 区域中央的文件图标
    QLabel *m_hint = nullptr;    // 提示文字
};

#endif // DROPAREA_H
