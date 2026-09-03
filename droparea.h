#ifndef DROPAREA_H
#define DROPAREA_H

#include <QFrame>
#include <QLabel>

/**
 * @brief 拖拽接收区：重写拖拽事件实现"拖入高亮、松手发出 fileDropped 信号"。
 */
class DropArea : public QFrame
{
    Q_OBJECT
public:
    explicit DropArea(QWidget *parent = nullptr);
    void setHint(const QString &text);   // 设置区域中央的提示文字（按窗口角色定制）

signals:
    void fileDropped(const QString &filePath);   // 每个被拖入的本地文件发一次

protected:
    // 拖拽事件虚函数重写
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setDragOver(bool on);   // 切换高亮（动态属性 + QSS）

    QLabel *m_icon = nullptr;    // 区域中央的文件图标
    QLabel *m_hint = nullptr;    // 提示文字
};

#endif // DROPAREA_H
