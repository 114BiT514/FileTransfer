#-----------------------------------------------------------------
# 图标生成工具（可选，不影响主程序）
# 用 QPainter 画出 resources/icons/ 下的 PNG 图标，再次自定义时运行：
#   cd tools && mkdir build && cd build && qmake ../gen_icons.pro && make
#   QT_QPA_PLATFORM=offscreen ./gen_icons   （需在工程根目录运行）
#-----------------------------------------------------------------
TEMPLATE = app
TARGET   = gen_icons
CONFIG  += c++17
QT      += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

SOURCES += gen_icons.cpp
