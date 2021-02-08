#-------------------------------------------------
#
# Project created by QtCreator 2018-05-26T16:57:32
#
#-------------------------------------------------

QT       += core gui serialport network multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

TARGET = wfview
TEMPLATE = app

QMAKE_CXXFLAGS += -march=native

CONFIG(debug, release|debug) {
# For Debug builds only:

} else {
# For Release builds only:
QMAKE_CXXFLAGS += -s
QMAKE_CXXFLAGS += -fvisibility=hidden
QMAKE_CXXFLAGS += -fvisibility-inlines-hidden
QMAKE_LFLAGS += -O2 -march=native -s
}

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QCUSTOMPLOT_COMPILE_LIBRARY

linux:DEFINES += HOST=\\\"`hostname`\\\" UNAME=\\\"`whoami`\\\"

linux:DEFINES += GITSHORT="\\\"$(shell git -C $$PWD rev-parse --short HEAD)\\\""

win32:INCLUDEPATH += c:/qcustomplot
win32:DEFINES += HOST=1
win32:DEFINES += UNAME=1
win32:DEFINES += GITSHORT=1


RESOURCES += qdarkstyle/style.qrc \
    resources/resources.qrc

# Why doesn't this seem to do anything?
DISTFILES += resources/wfview.png \
    resources/install.sh
DISTFILES += resources/wfview.desktop

linux:QMAKE_POST_LINK += cp ../wfview/resources/wfview.png .;
linux:QMAKE_POST_LINK += cp ../wfview/resources/wfview.desktop .;
linux:QMAKE_POST_LINK += cp ../wfview/resources/install.sh .;
linux:QMAKE_POST_LINK += cp -r ../wfview/qdarkstyle .;
linux:QMAKE_POST_LINK += chmod 755 install.sh;
linux:QMAKE_POST_LINK += echo; echo; echo "Run install.sh as root from the build directory to install."; echo; echo;


# Do not do this, it will hang on start:
# CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT

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
    freqmemory.cpp \
    rigidentities.cpp \
	udphandler.cpp \
	logcategories.cpp \
    rxaudiohandler.cpp

HEADERS  += wfmain.h \
    commhandler.h \
    rigcommander.h \
    freqmemory.h \
    rigidentities.h \
	udphandler.h \
	logcategories.h \
    rxaudiohandler.h

FORMS    += wfmain.ui


