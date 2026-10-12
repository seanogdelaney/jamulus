TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
QT -= gui
SOURCES += polyin_fault_test.cpp ../src/polyinwire.cpp ../src/polyinplayout.cpp
INCLUDEPATH += ../src
