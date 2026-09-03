/* 图标生成工具：用 QPainter 画出项目所需的 PNG 图标（96x96，线框风格）。
 * 生成的 PNG 放入 resources/icons/ 并打包进 resources.qrc，
 * 这样不依赖 qtsvg 模块，任何 Qt 6 环境都能正确渲染。 */
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QDir>
#include <cstdio>

static QImage makeImage()
{
    QImage img(96, 96, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

static void saveIcon(const QImage &img, const QString &name)
{
    QDir().mkpath(QStringLiteral("icons"));
    img.save(QStringLiteral("icons/") + name);
    std::printf("generated icons/%s\n", qPrintable(name));
}

static QPen pen(const QColor &c, qreal w = 1.8)
{
    QPen p(c, w);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    const QColor blue(QStringLiteral("#2f81f7"));
    const QColor gray(QStringLiteral("#8aa2bd"));

    {   // connect：插头（连接）
        QImage img = makeImage();
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(4, 4);
        p.setPen(pen(blue));
        p.drawLine(QPointF(9, 3), QPointF(9, 8));
        p.drawLine(QPointF(15, 3), QPointF(15, 8));
        p.drawRoundedRect(QRectF(6, 8, 12, 7), 2, 2);
        p.drawLine(QPointF(12, 15), QPointF(12, 20));
        p.end();
        saveIcon(img, QStringLiteral("connect.png"));
    }
    {   // server：机架（监听）
        QImage img = makeImage();
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(4, 4);
        p.setPen(pen(blue));
        p.drawRoundedRect(QRectF(3, 4, 18, 7), 2, 2);
        p.drawRoundedRect(QRectF(3, 13, 18, 7), 2, 2);
        p.setBrush(blue);
        p.drawEllipse(QPointF(7, 7.5), 0.7, 0.7);
        p.drawEllipse(QPointF(7, 16.5), 0.7, 0.7);
        p.end();
        saveIcon(img, QStringLiteral("server.png"));
    }
    {   // send：纸飞机（发送）
        QImage img = makeImage();
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(4, 4);
        p.setPen(pen(blue));
        p.drawLine(QPointF(21.5, 2.5), QPointF(10.8, 13.2));
        QPainterPath plane;
        plane.moveTo(21.5, 2.5);
        plane.lineTo(14.8, 21.5);
        plane.lineTo(10.8, 13.2);
        plane.lineTo(2.5, 9.2);
        plane.closeSubpath();
        p.drawPath(plane);
        p.end();
        saveIcon(img, QStringLiteral("send.png"));
    }
    {   // folder：文件夹（保存目录）
        QImage img = makeImage();
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(4, 4);
        p.setPen(pen(blue));
        QPainterPath tab;
        tab.moveTo(3, 7.5);
        tab.lineTo(5, 4.5);
        tab.lineTo(9.5, 4.5);
        tab.lineTo(11.5, 7.5);
        p.drawPath(tab);
        p.drawRoundedRect(QRectF(3, 7.5, 18, 11.5), 2, 2);
        p.end();
        saveIcon(img, QStringLiteral("folder.png"));
    }
    {   // settings：三个滑杆（设置）
        QImage img = makeImage();
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(4, 4);
        p.setPen(pen(blue));
        p.drawLine(QPointF(4, 6), QPointF(20, 6));
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
        p.drawLine(QPointF(4, 18), QPointF(20, 18));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(15, 6), 2.4, 2.4);
        p.drawEllipse(QPointF(7, 12), 2.4, 2.4);
        p.drawEllipse(QPointF(15, 18), 2.4, 2.4);
        p.end();
        saveIcon(img, QStringLiteral("settings.png"));
    }
    {   // exit：门 + 向外箭头（退出）
        QImage img = makeImage();
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(4, 4);
        p.setPen(pen(blue));
        QPainterPath door;
        door.moveTo(9, 3);
        door.lineTo(5.5, 3);
        door.quadTo(3.5, 3, 3.5, 5);
        door.lineTo(3.5, 19);
        door.quadTo(3.5, 21, 5.5, 21);
        door.lineTo(9, 21);
        p.drawPath(door);
        p.drawLine(QPointF(12, 12), QPointF(21, 12));
        p.drawLine(QPointF(17, 8), QPointF(21, 12));
        p.drawLine(QPointF(17, 16), QPointF(21, 12));
        p.end();
        saveIcon(img, QStringLiteral("exit.png"));
    }
    {   // file：文件（拖拽区提示）
        QImage img = makeImage();
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(4, 4);
        p.setPen(pen(gray));
        p.drawRoundedRect(QRectF(5, 2, 14, 20), 2, 2);
        QPainterPath fold;
        fold.moveTo(14, 2);
        fold.lineTo(14, 8);
        fold.lineTo(20, 8);
        p.drawPath(fold);
        p.end();
        saveIcon(img, QStringLiteral("file.png"));
    }
    {   // app：双向传输箭头（应用图标）
        QImage img = makeImage();
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(4, 4);
        p.setPen(pen(blue));
        p.drawLine(QPointF(8, 20), QPointF(8, 4.5));
        p.drawLine(QPointF(4.5, 8), QPointF(8, 4.5));
        p.drawLine(QPointF(11.5, 8), QPointF(8, 4.5));
        p.drawLine(QPointF(16, 4), QPointF(16, 19.5));
        p.drawLine(QPointF(12.5, 16), QPointF(16, 19.5));
        p.drawLine(QPointF(19.5, 16), QPointF(16, 19.5));
        p.end();
        saveIcon(img, QStringLiteral("app.png"));
    }

    std::printf("all icons done\n");
    return 0;
}
