#include <QApplication>
#include <QFile>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("QtCourse"));
    QApplication::setApplicationName(QStringLiteral("FileTransfer"));

    // 应用图标（Qt 资源系统）
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    // 加载 QSS 全局样式表（打包在资源系统里），实现按钮悬停、圆角、拖拽高亮等效果
    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    MainWindow w;
    w.show();
    return app.exec();
}
