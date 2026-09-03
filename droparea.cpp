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
    setAcceptDrops(true);
    setObjectName(QStringLiteral("dropArea"));

    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(8);

    m_icon = new QLabel(this);
    m_icon->setPixmap(QIcon(QStringLiteral(":/icons/file.png")).pixmap(48, 48));
    m_icon->setAlignment(Qt::AlignCenter);

    m_hint = new QLabel(this);
    m_hint->setText(QStringLiteral("将文件拖拽到此处"));
    m_hint->setAlignment(Qt::AlignCenter);

    layout->addWidget(m_icon);
    layout->addWidget(m_hint);
}

void DropArea::setHint(const QString &text)
{
    m_hint->setText(text);
}

/* 拖入边界：含文件则接受并高亮 */
void DropArea::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        setDragOver(true);
    } else {
        event->ignore();
    }
}

void DropArea::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void DropArea::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDragOver(false);
    QFrame::dragLeaveEvent(event);
}

/* 松开鼠标：把拖入的本地文件逐个发信号出去 */
void DropArea::dropEvent(QDropEvent *event)
{
    setDragOver(false);
    const auto urls = event->mimeData()->urls();
    int accepted = 0;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        emit fileDropped(url.toLocalFile());
        ++accepted;
    }
    if (accepted > 0)
        event->acceptProposedAction();
    else
        event->ignore();
}

/* 动态属性配合 QSS 选择器实现高亮；属性变化后需重新应用样式 */
void DropArea::setDragOver(bool on)
{
    setProperty("dragOver", on);
    style()->unpolish(this);
    style()->polish(this);
}
