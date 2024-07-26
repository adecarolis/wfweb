#-------------------------------------------------
#
# Project created by QtCreator 2018-05-26T16:57:32
#
#-------------------------------------------------

QT       += core serialport network multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

TARGET = wfserver
TEMPLATE = app

CONFIG += console

DEFINES += WFVIEW_VERSION=\\\"1.65\\\"

DEFINES += BUILD_WFSERVER


CONFIG(debug, release|debug) {
  # For Debug builds only:
  linux:QMAKE_CXXFLAGS += -faligned-new
  win32 {
    contains(QMAKE_TARGET.arch, x86_64) {
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\x64\Debug\portaudio_x64.dll wfview-debug $$escape_expand(\\n\\t))
      LIBS += -L../portaudio/msvc/X64/Debug/ -lportaudio_x64
      LIBS += -L../opus/win32/VS2015/x64/Debug/ -lopus -lole32
    } else {
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\win32\Debug\portaudio_x86.dll wfview-debug\$$escape_expand(\\n\\t))
      LIBS += -L../portaudio/msvc/Win32/Debug/ -lportaudio_x86
      LIBS += -L../opus/win32/VS2015/Win32/Debug/ -lopus -lole32
    }
    DESTDIR = wfview-release
  }
} else {
  # For Release builds only:
  linux:QMAKE_CXXFLAGS += -s
  linux:QMAKE_CXXFLAGS += -fvisibility=hidden
  linux:QMAKE_CXXFLAGS += -fvisibility-inlines-hidden
  linux:QMAKE_CXXFLAGS += -faligned-new
  linux:QMAKE_LFLAGS += -O2 -s

  win32 {
    contains(QMAKE_TARGET.arch, x86_64) {
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\x64\Release\portaudio_x64.dll wfview-release $$escape_expand(\\n\\t))
      LIBS += -L../portaudio/msvc/X64/Release/ -lportaudio_x64
      LIBS += -L../opus/win32/VS2015/x64/Release/ -lopus -lole32
    } else {
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\win32\Release\portaudio_x86.dll wfview-release $$escape_expand(\\n\\t))
      LIBS += -L../portaudio/msvc/Win32/Release/ -lportaudio_x86
      LIBS += -L../opus/win32/VS2015/Win32/Release/ -lopus -lole32
    }
    DESTDIR = wfview-debug
  }
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
DEFINES += QCUSTOMPLOT_COMPILE_LIBRARY


# These defines are used for the resampler
equals(QT_ARCH, i386): DEFINES += USE_SSE
equals(QT_ARCH, i386): DEFINES += USE_SSE2
equals(QT_ARCH, arm): DEFINES += USE_NEON
DEFINES += OUTSIDE_SPEEX
DEFINES += RANDOM_PREFIX=wf

isEmpty(PREFIX) {
  PREFIX = /usr/local
}

# These defines are used for the Eigen library
DEFINES += EIGEN_MPL2_ONLY
DEFINES += EIGEN_DONT_VECTORIZE #Clear vector flags
equals(QT_ARCH, i386): win32:DEFINES += EIGEN_VECTORIZE_SSE3
equals(QT_ARCH, x86_64): DEFINES += EIGEN_VECTORIZE_SSE3

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

!win32:DEFINES += GITSHORT="\\\"$(shell git -C $$PWD rev-parse --short HEAD)\\\""
win32:DEFINES += GITSHORT=\\\"$$system(git -C $$PWD rev-parse --short HEAD)\\\"

win32:DEFINES += HOST=\\\"wfview.org\\\"
win32:DEFINES += UNAME=\\\"build\\\"


RESOURCES += qdarkstyle/style.qrc \
    resources/resources.qrc

unix:target.path = $$PREFIX/bin
INSTALLS += target

linux:LIBS += -L./ -lopus
macx:LIBS += -framework CoreAudio -framework CoreFoundation -lpthread -lopus 

!linux:INCLUDEPATH += ../opus/include

!linux:INCLUDEPATH += ../eigen
!linux:INCLUDEPATH += ../r8brain-free-src

INCLUDEPATH += resampler

SOURCES += main.cpp\
    servermain.cpp \
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
    udpserver.cpp \
    pttyhandler.cpp \
    resampler/resample.c \
    rigctld.cpp \
    tcpserver.cpp \
    keyboard.cpp \
    audiodevices.cpp

HEADERS  += servermain.h \
    commhandler.h \
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
    udpserver.h \
    packettypes.h \
    pttyhandler.h \
    resampler/speex_resampler.h \
    resampler/arch.h \
    resampler/resample_sse.h \
    repeaterattributes.h \
    rigctld.h \
    ulaw.h \
    tcpserver.h \
    audiotaper.h \
    keyboard.h \
    wfviewtypes.h \
    audiodevices.h
