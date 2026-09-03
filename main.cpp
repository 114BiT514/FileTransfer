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

    // 加载 QSS 样式表
    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    // 先选角色，再打开对应的独立窗口
    StartDialog start;
    if (start.exec() != QDialog::Accepted)
        return 0;

    if (start.selectedRole() == AppRole::Server) {
        ServerWindow w;
        w.show();
        return app.exec();
    }

    ClientWindow w;
    w.show();
    return app.exec();
}
