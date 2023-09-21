#-------------------------------------------------
#
# Project created by QtCreator 2012-03-04T23:24:55
#
#-------------------------------------------------

QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

CONFIG += c++17
QMAKE_CXXFLAGS += -std=c++17

TARGET = mfm_bwatcher
TEMPLATE = app

SOURCES += main.cpp\
        axistag.cpp \
        conf.cpp \
        gotodialog.cpp \
        incidentmarker.cpp \
        mainwindow.cpp  \
        pointofinterest.cpp \
        qcustomplot.cpp

HEADERS  += mainwindow.h  \
    axistag.h \
    conf.h \
    gotodialog.h \
    incidentmarker.h \
    pointofinterest.h \
    qcustomplot.h

FORMS    += mainwindow.ui \
    gotodialog.ui

