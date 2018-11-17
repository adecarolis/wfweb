#-------------------------------------------------
#
# Project created by QtCreator 2018-05-26T16:57:32
#
#-------------------------------------------------

QT       += core gui serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

TARGET = wfview
TEMPLATE = app

QMAKE_CXXFLAGS += -march=native

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QCUSTOMPLOT_COMPILE_LIBRARY

DEFINES += HOST=\\\"`hostname`\\\" UNAME=\\\"`whoami`\\\"

DEFINES += GITSHORT="\\\"$(shell git -C $$PWD rev-parse --short HEAD)\\\""


RESOURCES += qdarkstyle/style.qrc


CONFIG(debug, release|debug) {
  win32:QCPLIB = qcustomplotd1
  else: QCPLIB = qcustomplotd
} else {
  win32:QCPLIB = qcustomplot1
  else: QCPLIB = qcustomplot
}

QCPLIB = qcustomplot

LIBS += -L./ -l$$QCPLIB


SOURCES += main.cpp\
        wfmain.cpp \
    commhandler.cpp \
    rigcommander.cpp \
    freqmemory.cpp

HEADERS  += wfmain.h \
    ../../../../../usr/include/qcustomplot.h \
    commhandler.h \
    rigcommander.h \
    freqmemory.h

FORMS    += wfmain.ui
