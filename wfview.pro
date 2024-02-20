#-------------------------------------------------
#
# Project created by QtCreator 2018-05-26T16:57:32
#
#-------------------------------------------------

QT       += core gui serialport network multimedia xml

#QT += sql
#DEFINES += USESQL

#Uncomment The following line to enable USB controllers (Shuttle/RC-28 etc.)
DEFINES += USB_CONTROLLER

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

contains(DEFINES,USB_CONTROLLER){
    lessThan(QT_MAJOR_VERSION, 6): QT += gamepad
}

TARGET = wfview
TEMPLATE = app

DEFINES += WFVIEW_VERSION=\\\"1.65\\\"

DEFINES += BUILD_WFVIEW

CONFIG(debug, release|debug) {
    # For Debug builds only:
    linux:QMAKE_CXXFLAGS += -faligned-new
    win32:DESTDIR = wfview-release
} else {
    # For Release builds only:
    linux:QMAKE_CXXFLAGS += -s
    linux:QMAKE_CXXFLAGS += -fvisibility=hidden
    linux:QMAKE_CXXFLAGS += -fvisibility-inlines-hidden
    linux:QMAKE_CXXFLAGS += -faligned-new
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

linux:LIBS += -lpulse -lpulse-simple -lrtaudio -lpthread -ludev

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
macx:QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.14
macx:QMAKE_APPLE_DEVICE_ARCHS = x86_64 arm64
macx:MY_ENTITLEMENTS.name = CODE_SIGN_ENTITLEMENTS
macx:MY_ENTITLEMENTS.value = ../wfview/resources/wfview.entitlements
macx:QMAKE_MAC_XCODE_SETTINGS += MY_ENTITLEMENTS
macx:QMAKE_INFO_PLIST = ../wfview/resources/Info.plist

QMAKE_TARGET_BUNDLE_PREFIX = org.wfview

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
unix:DISTFILES += resources/unix_icons/wfview.png \
    resources/install.sh
unix:DISTFILES += resources/wfview.desktop
unix:DISTFILES += resources/org.wfview.wfview.metainfo.xml

unix:applications.files = resources/wfview.desktop
unix:applications.path = $$PREFIX/share/applications
INSTALLS += applications

unix:icons.files = resources/unix_icons/wfview.png
unix:icons.path = $$PREFIX/share/icons/hicolor/256x256/apps
INSTALLS += icons

unix:metainfo.files = resources/org.wfview.wfview.metainfo.xml
unix:metainfo.path = $$PREFIX/share/metainfo
INSTALLS += metainfo

unix:stylesheets.files = qdarkstyle
unix:stylesheets.path = $$PREFIX/share/wfview
INSTALLS += stylesheets

macx:LIBS += -framework CoreAudio -framework CoreFoundation -lpthread -lopus

# Do not do this, it will hang on start:
# CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT

CONFIG(debug, release|debug) {
  linux:LIBS += $$system("/sbin/ldconfig -p | awk '/libQCustomPlotd.so/ {print \"-lQCustomPlotd\"}'")
  linux:LIBS += $$system("/sbin/ldconfig -p | awk '/libqcustomplotd2.so/ {print \"-lqcustomplotd2\"}'")
  linux:LIBS += $$system("/sbin/ldconfig -p | awk '/libqcustomplotd.so/ {print \"-lqcustomplotd\"}'")
  macos:LIBS += -lqcustomplotd
  win32:LIBS += -lqcustomplotd2
  win32 {
    contains(QMAKE_TARGET.arch, x86_64) {
      LIBS += -L../opus/win32/VS2015/x64/DebugDLL/
      LIBS += -L../qcustomplot/x64
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\qcustomplot\x64\qcustomplotd2.dll wfview-debug $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\x64\Debug\portaudio_x64.dll wfview-debug $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\hidapi\windows\X64\Debug\hidapi.dll wfview-debug $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\opus\win32\VS2015\x64\DebugDLL\opus.dll wfview-debug $$escape_expand(\\n\\t))
      LIBS += -L../portaudio/msvc/X64/Debug/ -lportaudio_x64
      contains(DEFINES,USB_CONTROLLER){
            LIBS += -L../hidapi/windows/x64/debug -lhidapi
      }
    } else {
      LIBS += -L../opus/win32/VS2015/win32/DebugDLL/
      LIBS += -L../qcustomplot/win32
      LIBS += -L../portaudio/msvc/Win32/Debug/ -lportaudio_x86
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y .\qcustomplot\win32\qcustomplotd2.dll wfview-debug $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\win32\Debug\portaudio_x86.dll wfview-debug\$$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\hidapi\windows\Debug\hidapi.dll wfview-debug $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\opus\win32\VS2015\win32\DebugDLL\opus.dll wfview-debug $$escape_expand(\\n\\t))
     contains(DEFINES,USB_CONTROLLER){
            LIBS += -L../hidapi/windows/debug -lhidapi
      }
    }
  }
} else {
  linux:LIBS += $$system("/sbin/ldconfig -p | awk '/libQCustomPlot.so/ {print \"-lQCustomPlot\"}'")
  linux:LIBS += $$system("/sbin/ldconfig -p | awk '/libqcustomplot2.so/ {print \"-lqcustomplot2\"}'")
  linux:LIBS += $$system("/sbin/ldconfig -p | awk '/libqcustomplot.so/ {print \"-lqcustomplot\"}'")
  macos:LIBS += -lqcustomplot
  win32:LIBS += -lqcustomplot2
  win32 {
    contains(QMAKE_TARGET.arch, x86_64) {
      LIBS += -L../opus/win32/VS2015/x64/ReleaseDLL/
      LIBS += -L../qcustomplot/x64
      LIBS += -L../portaudio/msvc/X64/Release/ -lportaudio_x64
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\qcustomplot\x64\qcustomplot2.dll wfview-release $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\x64\Release\portaudio_x64.dll wfview-release $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\hidapi\windows\X64\Release\hidapi.dll wfview-release $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\opus\win32\VS2015\x64\ReleaseDLL\opus.dll wfview-release $$escape_expand(\\n\\t))
      contains(DEFINES,USB_CONTROLLER){
            LIBS += -L../hidapi/windows/x64/release -lhidapi
      }
    } else {
      LIBS += -L../opus/win32/VS2015/win32/ReleaseDLL/
      LIBS += -L../qcustomplot/win32
      LIBS += -L../portaudio/msvc/Win32/Release/ -lportaudio_x86
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\qcustomplot\win32\qcustomplot2.dll wfview-release $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\win32\Release\portaudio_x86.dll wfview-release $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\hidapi\windows\Release\hidapi.dll wfview-release $$escape_expand(\\n\\t))
      QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\opus\win32\VS2015\win32\ReleaseDLL\opus.dll wfview-release $$escape_expand(\\n\\t))
      contains(DEFINES,USB_CONTROLLER){
            win32:LIBS += -L../hidapi/windows/release -lhidapi
      }
    }
  }
}

contains(DEFINES,USB_CONTROLLER){
    linux:LIBS += -L./ -lhidapi-libusb
    macx:LIBS += -lhidapi
    win32:INCLUDEPATH += ../hidapi/hidapi
}

!win32:LIBS += -L./ -lopus
win32:LIBS += -lopus -lole32 -luser32

#macx:SOURCES += ../qcustomplot/qcustomplot.cpp 
#macx:HEADERS += ../qcustomplot/qcustomplot.h

win32:INCLUDEPATH += ../qcustomplot
!linux:INCLUDEPATH += ../opus/include

!linux:INCLUDEPATH += ../eigen
!linux:INCLUDEPATH += ../r8brain-free-src

INCLUDEPATH += resampler

SOURCES += main.cpp\
    cwsender.cpp \
    cwsidetone.cpp \
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
    usbcontroller.cpp \
    controllersetup.cpp \
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
    cwsender.h \
    cwsidetone.h \
    loggingwindow.h \
    prefs.h \
    printhex.h \
    rigcommander.h \
    freqmemory.h \
    rigidentities.h \
    sidebandchooser.h \
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
    usbcontroller.h \
    controllersetup.h \
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
    cwsender.ui \
    loggingwindow.ui \
    satellitesetup.ui \
    selectradio.ui \
    repeatersetup.ui \
    transceiveradjustments.ui \
    controllersetup.ui \
    aboutbox.ui



