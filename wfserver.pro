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

DEFINES += WFVIEW_VERSION=\\\"1.2d\\\"

DEFINES += BUILD_WFSERVER

CONFIG(debug, release|debug) {
# For Debug builds only:
QMAKE_CXXFLAGS += -faligned-new
WIN32:DESTDIR = wfview-debug

} else {
# For Release builds only:
linux:QMAKE_CXXFLAGS += -s
QMAKE_CXXFLAGS += -fvisibility=hidden
QMAKE_CXXFLAGS += -fvisibility-inlines-hidden
QMAKE_CXXFLAGS += -faligned-new
linux:QMAKE_LFLAGS += -O2 -s
WIN32:DESTDIR = wfview-release
}

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

# Choose audio system, uses QTMultimedia if both are commented out.
# DEFINES += RTAUDIO
# DEFINES += PORTAUDIO

contains(DEFINES, RTAUDIO) {
	# RTAudio defines
	win32:DEFINES += __WINDOWS_WASAPI__
	#win32:DEFINES += __WINDOWS_DS__ # Requires DirectSound libraries
	linux:DEFINES += __LINUX_ALSA__
	#linux:DEFINES += __LINUX_OSS__
	#linux:DEFINES += __LINUX_PULSE__
	macx:DEFINES += __MACOSX_CORE__
	win32:SOURCES += ../rtaudio/RTAudio.cpp
	win32:HEADERS += ../rtaudio/RTAUdio.h
	!linux:INCLUDEPATH += ../rtaudio
	linux:LIBS += -lpulse -lpulse-simple -lrtaudio -lpthread
}

contains(DEFINES, PORTAUDIO) {
	CONFIG(debug, release|debug) {
  		win32:LIBS += -L../portaudio/msvc/Win32/Debug/ -lportaudio_x86
	} else {
  		win32:LIBS += -L../portaudio/msvc/Win32/Release/ -lportaudio_x86
	}
	win32:INCLUDEPATH += ../portaudio/include
	!win32:LIBS += -lportaudio
}

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

# Do not do this, it will hang on start:
# CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT

CONFIG(debug, release|debug) {
  win32:LIBS += -L../opus/win32/VS2015/Win32/Debug/ -lopus
} else {
  win32:LIBS += -L../opus/win32/VS2015/Win32/Release/ -lopus
}

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
    udphandler.cpp \
    logcategories.cpp \
    audiohandler.cpp \
    udpserver.cpp \
    pttyhandler.cpp \
    resampler/resample.c \
    rigctld.cpp \
    tcpserver.cpp \

HEADERS  += servermain.h \
    commhandler.h \
    rigcommander.h \
    freqmemory.h \
    rigidentities.h \
    udphandler.h \
    logcategories.h \
    audiohandler.h \
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
    audiotaper.h 
