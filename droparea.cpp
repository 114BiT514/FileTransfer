#include "droparea.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

DropArea::DropArea(QWidget *parent)
    : QFrame(parent)
{
    setAcceptDrops(true);            // 声明：本控件愿意接收拖拽事件
    setObjectName(QStringLiteral("dropArea"));

    // 区域中央放置图标与提示文字
    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(8);

    m_icon = new QLabel(this);
    m_icon->setPixmap(QIcon(QStringLiteral(":/icons/file.png")).pixmap(48, 48));
    m_icon->setAlignment(Qt::AlignCenter);

    m_hint = new QLabel(this);
    m_hint->setText(QStringLiteral("将文件拖拽到此处\n客户端：松开鼠标即发送\n服务端：松开鼠标保存到接收目录"));
    m_hint->setAlignment(Qt::AlignCenter);

    layout->addWidget(m_icon);
    layout->addWidget(m_hint);
}

/* 文件拖入边界：判断是否含本地文件 URL，是则接受并高亮 */
void DropArea::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        setDragOver(true);           // 高亮反馈（QSS 动态属性 dragOver=true）
    } else {
        event->ignore();             // 拖入的不是文件，忽略
    }
}

/* 文件在区域内移动：持续接受，保持高亮 */
void DropArea::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

/* 文件拖离区域 / 拖拽被取消：取消高亮 */
void DropArea::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDragOver(false);
    QFrame::dragLeaveEvent(event);
}

/* 松开鼠标放下：取出所有本地文件路径，逐个发信号交给主窗口处理 */
void DropArea::dropEvent(QDropEvent *event)
{
    setDragOver(false);
    const auto urls = event->mimeData()->urls();
    int accepted = 0;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        emit fileDropped(url.toLocalFile());   // 交给主窗口决定"发送"还是"保存"
        ++accepted;
    }
    if (accepted > 0)
        event->acceptProposedAction();
    else
        event->ignore();
}

/* 通过动态属性配合 QSS 选择器（DropArea[dragOver="true"]）实现高亮，
 * 属性变化后必须 unpolish + polish 才能让样式表重新计算。 */
void DropArea::setDragOver(bool on)
{
    setProperty("dragOver", on);
    style()->unpolish(this);
    style()->polish(this);
}
