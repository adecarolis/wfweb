#-------------------------------------------------
#
# Project created by QtCreator 2018-05-26T16:57:32
#
#-------------------------------------------------

QT       += core gui serialport network multimedia xml

#QT += sql
#DEFINES += USESQL

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

TARGET = wfview
TEMPLATE = app

DEFINES += WFVIEW_VERSION=\\\"1.55\\\"

DEFINES += BUILD_WFVIEW


CONFIG(debug, release|debug) {
    # For Debug builds only:
    QMAKE_CXXFLAGS += -faligned-new
    win32:DESTDIR = wfview-release
} else {
    # For Release builds only:
    linux:QMAKE_CXXFLAGS += -s
    QMAKE_CXXFLAGS += -fvisibility=hidden
    QMAKE_CXXFLAGS += -fvisibility-inlines-hidden
    QMAKE_CXXFLAGS += -faligned-new
    linux:QMAKE_LFLAGS += -O2 -s
    win32:DESTDIR = wfview-debug
}


# RTAudio defines
win32:DEFINES += __WINDOWS_WASAPI__
#win32:DEFINES += __WINDOWS_DS__ # Requires DirectSound libraries
#linux:DEFINES += __LINUX_ALSA__
#linux:DEFINES += __LINUX_OSS__
linux:DEFINES += __LINUX_PULSE__
macx:DEFINES += __MACOSX_CORE__
!linux:SOURCES += ../rtaudio/RTAudio.cpp
!linux:HEADERS += ../rtaudio/RTAUdio.h
!linux:INCLUDEPATH += ../rtaudio

linux:LIBS += -lpulse -lpulse-simple -lrtaudio -lpthread

win32:INCLUDEPATH += ../portaudio/include
!win32:LIBS += -lportaudio

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QCUSTOMPLOT_USE_LIBRARY


# These defines are used for the resampler
equals(QT_ARCH, i386): win32:DEFINES += USE_SSE
equals(QT_ARCH, i386): win32:DEFINES += USE_SSE2
equals(QT_ARCH, x86_64): DEFINES += USE_SSE
equals(QT_ARCH, x86_64): DEFINES += USE_SSE2
equals(QT_ARCH, arm): DEFINES += USE_NEON
DEFINES += OUTSIDE_SPEEX
DEFINES += RANDOM_PREFIX=wf

# These defines are used for the Eigen library
DEFINES += EIGEN_MPL2_ONLY
DEFINES += EIGEN_DONT_VECTORIZE #Clear vector flags
equals(QT_ARCH, i386): win32:DEFINES += EIGEN_VECTORIZE_SSE3
equals(QT_ARCH, x86_64): DEFINES += EIGEN_VECTORIZE_SSE3


isEmpty(PREFIX) {
  PREFIX = /usr/local
}

DEFINES += PREFIX=\\\"$$PREFIX\\\"

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

!win32:DEFINES += GITSHORT="\\\"$(shell git -C \"$$PWD\" rev-parse --short HEAD)\\\""
win32:DEFINES += GITSHORT=\\\"$$system(git -C $$PWD rev-parse --short HEAD)\\\"

win32:DEFINES += HOST=\\\"wfview.org\\\"
win32:DEFINES += UNAME=\\\"build\\\"


RESOURCES += qdarkstyle/style.qrc \
    resources/resources.qrc

unix:target.path = $$PREFIX/bin
INSTALLS += target

# Why doesn't this seem to do anything?
unix:DISTFILES += resources/wfview.png \
    resources/install.sh
unix:DISTFILES += resources/wfview.desktop

unix:applications.files = resources/wfview.desktop
unix:applications.path = $$PREFIX/share/applications
INSTALLS += applications

unix:pixmaps.files = resources/wfview.png
unix:pixmaps.path = $$PREFIX/share/pixmaps
INSTALLS += pixmaps

unix:stylesheets.files = qdarkstyle
unix:stylesheets.path = $$PREFIX/share/wfview
INSTALLS += stylesheets

# Do not do this, it will hang on start:
# CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT

CONFIG(debug, release|debug) {
  linux: QCPLIB = qcustomplotd
  !linux: QCPLIB = qcustomplotd2
  win32 {
    contains(QMAKE_TARGET.arch, x86_64) {
      LIBS += -L../opus/win32/VS2015/x64/Debug/
      LIBS += -L../qcustomplot/x64
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\qcustomplot\x64\qcustomplotd2.dll debug\$$escape_expand(\n\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\x64\Debug\portaudio_x64.dll debug\$$escape_expand(\n\t))
      win32:LIBS += -L../portaudio/msvc/X64/Debug/ -lportaudio_x64
    } else {
      LIBS += -L../opus/win32/VS2015/win32/Debug/
      LIBS += -L../qcustomplot/win32
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y .\qcustomplot\win32\qcustomplotd2.dll debug\$$escape_expand(\n\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\win32\Debug\portaudio_x86.dll debug\$$escape_expand(\n\t))
      win32:LIBS += -L../portaudio/msvc/Win32/Debug/ -lportaudio_x86 -lole32
    }
  }
} else {
  linux: QCPLIB = qcustomplot
  !linux: QCPLIB = qcustomplot2
  win32 {
    contains(QMAKE_TARGET.arch, x86_64) {
      LIBS += -L../opus/win32/VS2015/x64/Release/
      LIBS += -L../qcustomplot/x64
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\qcustomplot\x64\qcustomplot2.dll release\$$escape_expand(\n\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\x64\Release\portaudio_x64.dll release\$$escape_expand(\n\t))
      win32:LIBS += -L../portaudio/msvc/X64/Release/ -lportaudio_x64
    } else {
      LIBS += -L../opus/win32/VS2015/win32/Release/
      LIBS += -L../qcustomplot/win32
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\qcustomplot\win32\qcustomplot2.dll release\$$escape_expand(\n\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\win32\Release\portaudio_x86.dll release\$$escape_expand(\n\t))
      win32:LIBS += -L../portaudio/msvc/Win32/Release/ -lportaudio_x86 -lole32
    }
  }
}

linux:LIBS += -L./ -l$$QCPLIB -lopus
!linux:LIBS += -l$$QCPLIB -lopus
macx:LIBS += -framework CoreAudio -framework CoreFoundation -lpthread -lopus 

!linux:INCLUDEPATH += ../qcustomplot
!linux:INCLUDEPATH += ../opus/include

win32:INCLUDEPATH += ../eigen
win32:INCLUDEPATH += ../r8brain-free-src

INCLUDEPATH += resampler

SOURCES += main.cpp\
    loggingwindow.cpp \
    wfmain.cpp \
    commhandler.cpp \
    rigcommander.cpp \
    freqmemory.cpp \
    rigidentities.cpp \
    udpbase.cpp \
    udphandler.cpp \
    udpcivdata.cpp \
    udpaudio.cpp \
    logcategories.cpp \
    pahandler.cpp \
    rthandler.cpp \
    audiohandler.cpp \
    audioconverter.cpp \
    calibrationwindow.cpp \
    satellitesetup.cpp \
    udpserver.cpp \
    meter.cpp \
    qledlabel.cpp \
    pttyhandler.cpp \
    resampler/resample.c \
    repeatersetup.cpp \
    rigctld.cpp \
    transceiveradjustments.cpp \
    selectradio.cpp \
    tcpserver.cpp \
    cluster.cpp \
    database.cpp \
    aboutbox.cpp \
    audiodevices.cpp

HEADERS  += wfmain.h \
    colorprefs.h \
    commhandler.h \
    loggingwindow.h \
    prefs.h \
    rigcommander.h \
    freqmemory.h \
    rigidentities.h \
    udpbase.h \
    udphandler.h \
    udpcivdata.h \
    udpaudio.h \
    logcategories.h \
    pahandler.h \
    rthandler.h \
    audiohandler.h \
    audioconverter.h \
    calibrationwindow.h \
    satellitesetup.h \
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
    rigctld.h \
    rigstate.h \
    ulaw.h \
    transceiveradjustments.h \
    audiotaper.h \
    selectradio.h \
    tcpserver.h \
    cluster.h \
    database.h \
    aboutbox.h \
    wfviewtypes.h \
    audiodevices.h


FORMS    += wfmain.ui \
    calibrationwindow.ui \
    loggingwindow.ui \
    satellitesetup.ui \
    selectradio.ui \
    repeatersetup.ui \
    transceiveradjustments.ui \
    aboutbox.ui



