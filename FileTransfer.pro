#-----------------------------------------------------------------
# Qt 文件传输工具 —— 课程作业
#
# 客户端与服务端置于同一工程：同一个程序既可以"启动监听"充当服务端，
# 也可以"连接服务器"充当客户端。本地测试时启动两个实例即可互传文件。
#
# 界面(View)：  startdialog（角色选择）/ serverwindow / clientwindow / settingsdialog / droparea
# 传输逻辑(Model)：fileserver（接收端）/ fileclient（发送端）/ protocol.h（协议）
#-----------------------------------------------------------------

QT += core gui network        # network 模块提供 QTcpSocket / QTcpServer

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET   = FileTransfer
TEMPLATE = app
CONFIG  += c++17

# Windows 下 MSVC 编译器按 UTF-8 解析源码，保证中文注释/字符串不乱码
msvc: QMAKE_CXXFLAGS += /utf-8

SOURCES += \
    main.cpp \
    startdialog.cpp \
    serverwindow.cpp \
    clientwindow.cpp \
    settingsdialog.cpp \
    droparea.cpp \
    fileclient.cpp \
    fileserver.cpp

HEADERS += \
    startdialog.h \
    serverwindow.h \
    clientwindow.h \
    settingsdialog.h \
    droparea.h \
    fileclient.h \
    fileserver.h \
    protocol.h

# 所有界面均使用 .ui 文件设计（Qt Designer），uic 编译期生成 ui_*.h
FORMS += \
    startdialog.ui \
    serverwindow.ui \
    clientwindow.ui \
    settingsdialog.ui

# Qt 资源系统：图标与 QSS 样式表打包进可执行文件（:/:前缀访问）
RESOURCES += resources/resources.qrc
