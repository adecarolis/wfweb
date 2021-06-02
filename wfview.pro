#-------------------------------------------------
#
# Project created by QtCreator 2018-05-26T16:57:32
#
#-------------------------------------------------

QT       += core gui serialport network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

TARGET = wfview
TEMPLATE = app

CONFIG(debug, release|debug) {
# For Debug builds only:
QMAKE_CXXFLAGS += -faligned-new

} else {
# For Release builds only:
linux:QMAKE_CXXFLAGS += -s
QMAKE_CXXFLAGS += -fvisibility=hidden
QMAKE_CXXFLAGS += -fvisibility-inlines-hidden
QMAKE_CXXFLAGS += -faligned-new
linux:QMAKE_LFLAGS += -O2 -s
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

# RTAudio defines
win32:DEFINES += __WINDOWS_WASAPI__
#win32:DEFINES += __WINDOWS_DS__ # Requires DirectSound libraries
linux:DEFINES += __LINUX_ALSA__
#linux:DEFINES += __LINUX_OSS__
#linux:DEFINES += __LINUX_PULSE__
macx:DEFINES += __MACOSX_CORE__

macx:INCLUDEPATH += /usr/local/include /opt/local/include
macx:LIBS += -L/usr/local/lib -L/opt/local/lib

macx:ICON = ../wfview/resources/wfview.icns
win32:RC_ICONS = ../wfview/resources/wfview.ico
QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.13
QMAKE_TARGET_BUNDLE_PREFIX = org.wfview
MY_ENTITLEMENTS.name = CODE_SIGN_ENTITLEMENTS
MY_ENTITLEMENTS.value = ../wfview/resources/wfview.entitlements
QMAKE_MAC_XCODE_SETTINGS += MY_ENTITLEMENTS
QMAKE_INFO_PLIST = ../wfview/resources/Info.plist

!win32:DEFINES += HOST=\\\"`hostname`\\\" UNAME=\\\"`whoami`\\\"

!win32:DEFINES += GITSHORT="\\\"$(shell git -C $$PWD rev-parse --short HEAD)\\\""
win32:DEFINES += GITSHORT=\\\"$$system(git -C $$PWD rev-parse --short HEAD)\\\"

win32:DEFINES += HOST=\\\"wfview.org\\\"
win32:DEFINES += UNAME=\\\"build\\\"


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
  linux: QCPLIB = qcustomplotd
} else {
  linux: QCPLIB = qcustomplot
}

#linux:LIBS += -L./ -l$$QCPLIB -lpulse -lpulse-simple -lpthread
linux:LIBS += -L./ -l$$QCPLIB -lasound -lpthread -lrtaudio
macx:LIBS += -framework CoreAudio -framework CoreFoundation -lpthread

!linux:SOURCES += ../qcustomplot/qcustomplot.cpp
!linux:HEADERS += ../qcustomplot/qcustomplot.h
!linux:INCLUDEPATH += ../qcustomplot

INCLUDEPATH += opus-tools/src

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
    opus-tools/src/resample.c \
    repeatersetup.cpp \
    rigctld.cpp \
    ring/ring.cpp

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
    opus-tools/src/speex_resampler.h \
    opus-tools/src/arch.h \
    opus-tools/src/resample_sse.h \
    repeatersetup.h \
    repeaterattributes.h \
    rigctld.h \
    ulaw.h \
    ring/ring.h


FORMS    += wfmain.ui \
    calibrationwindow.ui \
    satellitesetup.ui \
    udpserversetup.ui \
    repeatersetup.ui



