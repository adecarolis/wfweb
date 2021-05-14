#-------------------------------------------------
#
# Project created by QtCreator 2018-05-26T16:57:32
#
#-------------------------------------------------

QT       += core gui serialport network multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

TARGET = wfview
TEMPLATE = app

CONFIG(debug, release|debug) {
# For Debug builds only:

} else {
# For Release builds only:
QMAKE_CXXFLAGS += -s
QMAKE_CXXFLAGS += -fvisibility=hidden
QMAKE_CXXFLAGS += -fvisibility-inlines-hidden
QMAKE_LFLAGS += -O2 -s
}

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QCUSTOMPLOT_COMPILE_LIBRARY

# These defines are used for the resampler
DEFINES += OUTSIDE_SPEEX
DEFINES += RANDOM_PREFIX=wf

macx:INCLUDEPATH += /usr/local/include /opt/local/include
macx:LIBS += -L/usr/local/lib -L/opt/local/lib

macx:ICON = wfview.icns
QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.13

!win32:DEFINES += HOST=\\\"`hostname`\\\" UNAME=\\\"`whoami`\\\"

!win32:DEFINES += GITSHORT="\\\"$(shell git -C $$PWD rev-parse --short HEAD)\\\""

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

!macx:LIBS += -L./ -l$$QCPLIB

macx:SOURCES += ../qcustomplot/qcustomplot.cpp
macx:HEADERS += ../qcustomplot/qcustomplot.h

SOURCES += main.cpp\
        wfmain.cpp \
    commhandler.cpp \
    rigcommander.cpp \
    freqmemory.cpp \
    rigidentities.cpp \
	udphandler.cpp \
	logcategories.cpp \
    audiohandler.cpp \
    calibrationwindow.cpp \
    satellitesetup.cpp \
    udpserversetup.cpp \
    udpserver.cpp \
    meter.cpp \
    qledlabel.cpp \
	pttyhandler.cpp \
	resampler/resample.c \
    repeatersetup.cpp \
	rigctld.cpp

HEADERS  += wfmain.h \
    commhandler.h \
    rigcommander.h \
    freqmemory.h \
    rigidentities.h \
	udphandler.h \
	logcategories.h \
    audiohandler.h \
    calibrationwindow.h \
    satellitesetup.h \
    udpserversetup.h \
	udpserver.h \
	packettypes.h \
    meter.h \
	qledlabel.h \
	pttyhandler.h \
	resampler/speex_resampler.h \
	resampler/arch.h \
	resampler/resample_sse.h \
    repeatersetup.h \
    repeaterattributes.h \
	rigctld.h


FORMS    += wfmain.ui \
    calibrationwindow.ui \
    satellitesetup.ui \
    udpserversetup.ui \
    repeatersetup.ui



