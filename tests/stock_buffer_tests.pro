TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
QT += core network
DEFINES += HEADLESS
INCLUDEPATH += ../src
SOURCES += stock_buffer_test.cpp ../src/buffer.cpp
