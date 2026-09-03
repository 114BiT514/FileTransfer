#include <QApplication>
#include <QFile>
#include "startdialog.h"
#include "serverwindow.h"
#include "clientwindow.h"

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

    // 1) 先弹出角色选择对话框：本次运行作为服务端还是客户端
    StartDialog start;
    if (start.exec() != QDialog::Accepted)
        return 0;

    // 2) 根据所选角色打开对应的独立窗口（两窗口复用同一套传输核心类）
    if (start.selectedRole() == AppRole::Server) {
        ServerWindow w;
        w.show();
        return app.exec();
    }

    ClientWindow w;
    w.show();
    return app.exec();
}
