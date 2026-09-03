#-----------------------------------------------------------------
# 协议自测程序（可选，不影响主程序提交）
# 验证 FileClient / FileServer 的分包组包、粘包处理、文本与文件传输。
# 构建与运行：
#   cd test && mkdir build && cd build && qmake ../selftest.pro && make
#   ./filetransfer-selftest    输出 SELFTEST PASS / SELFTEST FAIL
#-----------------------------------------------------------------
TEMPLATE = app
TARGET   = filetransfer-selftest
CONFIG  += console c++17
CONFIG  -= app_bundle
QT      -= gui
QT      += core network

INCLUDEPATH += ..

SOURCES += selftest.cpp \
    ../fileclient.cpp \
    ../fileserver.cpp

HEADERS += ../fileclient.h \
    ../fileserver.h \
    ../protocol.h
