#-------------------------------------------------
#
# Project created by QtCreator 2018-05-26T16:57:32
#
#-------------------------------------------------

QT       += core serialport network multimedia websockets


TARGET = wfweb
TEMPLATE = app

CONFIG += console
macx:CONFIG -= app_bundle
macx:CONFIG += c++17
win32:CONFIG += c++17

DEFINES += WFWEB_VERSION=\\\"0.5.2\\\"

DEFINES += BUILD_WFSERVER

# FTDI support requires the library from https://ftdichip.com/software-examples/ft4222h-software-examples/
DEFINES += FTDI_SUPPORT

CONFIG(debug, release|debug) {
  # For Debug builds only:
  linux:QMAKE_CXXFLAGS += -faligned-new
  win32 {
    isEmpty(VCPKG_DIR) {
      contains(QMAKE_TARGET.arch, x86_64) {
        LIBS += -L../opus/win32/VS2015/x64/DebugDLL/
        LIBS += -L../portaudio/msvc/X64/Debug/ -lportaudio_x64
        contains(DEFINES,FTDI_SUPPORT){
          LIBS += -L../LibFT4222-v1.4.7\imports\ftd2xx\dll\amd64 -lftd2xx
          LIBS += -L../LibFT4222-v1.4.7\imports\LibFT4222\dll\amd64 -lLibFT4222-64
          QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\LibFT4222-v1.4.7\imports\LibFT4222\dll\amd64\LibFT4222-64.dll wfweb-debug $$escape_expand(\\n\\t))
        }
        QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\x64\Debug\portaudio_x64.dll wfweb-debug $$escape_expand(\\n\\t))
        QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\opus\win32\VS2015\x64\DebugDLL\opus-0.dll wfweb-debug $$escape_expand(\\n\\t))
        QMAKE_POST_LINK +=$$quote(cmd /c xcopy /s/y rigs\*.* wfweb-debug\rigs\*.* $$escape_expand(\\n\\t))
      } else {
        LIBS += -L../opus/win32/VS2015/win32/DebugDLL/
        LIBS += -L../portaudio/msvc/Win32/Debug/ -lportaudio_x86
        contains(DEFINES,FTDI_SUPPORT){
          LIBS += -L../LibFT4222-v1.4.7\imports\ftd2xx\dll\i386 -lftd2xx
          LIBS += -L../LibFT4222-v1.4.7\imports\LibFT4222\dll\i386 -lLibFT4222
          QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\LibFT4222-v1.4.7\imports\LibFT4222\dll\i386\LibFT4222.dll wfweb-debug $$escape_expand(\\n\\t))
        }
        QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\win32\Debug\portaudio_x86.dll wfweb-debug $$escape_expand(\\n\\t))
        QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\opus\win32\VS2015\win32\DebugDLL\opus-0.dll wfweb-debug $$escape_expand(\\n\\t))
        QMAKE_POST_LINK +=$$quote(cmd /c xcopy /s/y rigs\*.* wfweb-debug\rigs\*.* $$escape_expand(\\n\\t))
      }
    }
    DESTDIR = wfweb-debug
  }
} else {
  # For Release builds only:
  linux:QMAKE_CXXFLAGS += -s
  linux:QMAKE_CXXFLAGS += -fvisibility=hidden
  linux:QMAKE_CXXFLAGS += -fvisibility-inlines-hidden
  linux:QMAKE_CXXFLAGS += -faligned-new
  linux:QMAKE_LFLAGS += -O2 -s

  win32 {
    isEmpty(VCPKG_DIR) {
      contains(QMAKE_TARGET.arch, x86_64) {
        LIBS += -L../opus/win32/VS2015/x64/ReleaseDLL/
        LIBS += -L../portaudio/msvc/X64/Release/ -lportaudio_x64
        contains(DEFINES,FTDI_SUPPORT){
          LIBS += -L../LibFT4222-v1.4.7\imports\ftd2xx\dll\amd64 -lftd2xx
          LIBS += -L../LibFT4222-v1.4.7\imports\LibFT4222\dll\amd64 -lLibFT4222-64
          QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\LibFT4222-v1.4.7\imports\LibFT4222\dll\amd64\LibFT4222-64.dll wfweb-release $$escape_expand(\\n\\t))
        }
        QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\x64\Release\portaudio_x64.dll wfweb-release $$escape_expand(\\n\\t))
        QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\opus\win32\VS2015\x64\ReleaseDLL\opus-0.dll wfweb-release $$escape_expand(\\n\\t))
        QMAKE_POST_LINK +=$$quote(cmd /c xcopy /s/y rigs\*.* wfweb-release\rigs\*.* $$escape_expand(\\n\\t))
      } else {
        LIBS += -L../opus/win32/VS2015/win32/ReleaseDLL/
        LIBS += -L../portaudio/msvc/Win32/Release/ -lportaudio_x86
        contains(DEFINES,FTDI_SUPPORT){
          LIBS += -L../LibFT4222-v1.4.7\imports\ftd2xx\dll\i386 -lftd2xx
          LIBS += -L../LibFT4222-v1.4.7\imports\LibFT4222\dll\i386 -lLibFT4222
          QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\LibFT4222-v1.4.7\imports\LibFT4222\dll\i386\LibFT4222.dll wfweb-release $$escape_expand(\\n\\t))
        }
        QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\portaudio\msvc\win32\Release\portaudio_x86.dll wfweb-release $$escape_expand(\\n\\t))
        QMAKE_POST_LINK +=$$quote(cmd /c copy /y ..\opus\win32\VS2015\win32\ReleaseDLL\opus-0.dll wfweb-release $$escape_expand(\\n\\t))
        QMAKE_POST_LINK +=$$quote(cmd /c xcopy /s/y rigs\*.* wfweb-release\rigs\*.* $$escape_expand(\\n\\t))
      }
    }
    DESTDIR = wfweb-release
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

win32:LIBS += -lopus -lole32 -luser32
!win32:LIBS += -lportaudio

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

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

macx:INCLUDEPATH += /usr/local/include /opt/local/include /opt/homebrew/include
macx:LIBS += -L/usr/local/lib -L/opt/local/lib -L/opt/homebrew/lib

QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.15
QMAKE_TARGET_BUNDLE_PREFIX = org.wfweb
QMAKE_INFO_PLIST = resources/Info.plist

macx{
    rigFiles.files = rigs
    rigFiles.path = Contents/MacOS
    QMAKE_BUNDLE_DATA += rigFiles
}

unix:rigs.files = rigs/*
unix:rigs.path = $$PREFIX/share/wfview/rigs
INSTALLS += rigs

!win32:DEFINES += HOST=\\\"`hostname`\\\" UNAME=\\\"`whoami`\\\"

!win32:DEFINES += GITSHORT="\\\"$(shell git -C $$PWD rev-parse --short HEAD)\\\""
win32:DEFINES += GITSHORT=\\\"$$system(git -C $$PWD rev-parse --short HEAD)\\\"

win32:DEFINES += HOST=\\\"wfweb.org\\\"
win32:DEFINES += UNAME=\\\"build\\\"


RESOURCES += resources/web.qrc

unix:target.path = $$PREFIX/bin
INSTALLS += target

unix:systemd.files = systemd/wfweb@.service
unix:systemd.path = $$PREFIX/lib/systemd/system
INSTALLS += systemd

linux:LIBS += -L./ -lopus
macx:LIBS += -framework CoreAudio -framework CoreFoundation -lpthread -lopus -lssl -lcrypto

# RADE V1 (radae_nopy) support.
# Auto-detects the radae_nopy submodule; override with RADAE_DIR env var or qmake arg.
# Build radae_nopy first:
#   Linux/macOS: cd resources/radae_nopy/build && cmake -DCMAKE_BUILD_TYPE=Release .. && make
#   Windows:     build custom Opus with cmake (see BUILDING-WINDOWS.md)
isEmpty(RADAE_DIR): RADAE_DIR = $$(RADAE_DIR)
isEmpty(RADAE_DIR): exists($$PWD/resources/radae_nopy/src/rade_api.h): RADAE_DIR = $$PWD/resources/radae_nopy
!isEmpty(RADAE_DIR) {
    isEmpty(RADAE_BUILD): RADAE_BUILD = $$RADAE_DIR/build
    # Check for built artifacts (platform-specific paths)
    OPUS_AUTOTOOLS = $$RADAE_BUILD/build_opus-prefix/src/build_opus/.libs/libopus.a
    OPUS_MSVC_LIB = $$RADAE_BUILD/opus_msvc_build/Release/opus.lib
    exists($$RADAE_BUILD/src/librade.so) | exists($$RADAE_BUILD/src/librade.dylib) | exists($$OPUS_AUTOTOOLS) | exists($$OPUS_MSVC_LIB) {
        DEFINES += RADE_SUPPORT
        INCLUDEPATH += $$RADAE_DIR/src
        # Custom Opus headers (LPCNet/FARGAN) from the CMake ExternalProject
        OPUS_SRC = $$RADAE_BUILD/build_opus-prefix/src/build_opus
        INCLUDEPATH += $$OPUS_SRC/dnn $$OPUS_SRC/celt $$OPUS_SRC/include $$OPUS_SRC
        macx|win32 {
            # macOS/Windows: compile rade sources directly into the binary (static link)
            macx:  DEFINES += IS_BUILDING_RADE_API=1
            win32: DEFINES += RADE_STATIC=1
            DEFINES += RADE_PYTHON_FREE=1
            SOURCES += \
                $$RADAE_DIR/src/rade_api_nopy.c \
                $$RADAE_DIR/src/rade_enc.c \
                $$RADAE_DIR/src/rade_dec.c \
                $$RADAE_DIR/src/rade_enc_data.c \
                $$RADAE_DIR/src/rade_dec_data.c \
                $$RADAE_DIR/src/rade_dsp.c \
                $$RADAE_DIR/src/rade_ofdm.c \
                $$RADAE_DIR/src/rade_bpf.c \
                $$RADAE_DIR/src/rade_acq.c \
                $$RADAE_DIR/src/rade_tx.c \
                $$RADAE_DIR/src/rade_rx.c
        } else {
            # Linux: link shared library
            LIBS += -L$$RADAE_BUILD/src -lrade
            QMAKE_RPATHDIR += $$RADAE_BUILD/src
        }
        # Custom Opus (with LPCNet/FARGAN) - replace standard opus with custom build
        win32 {
            LIBS -= -lopus
            exists($$OPUS_MSVC_LIB): LIBS += $$OPUS_MSVC_LIB
            else: LIBS += $$OPUS_AUTOTOOLS
        } else {
            LIBS += $$OPUS_AUTOTOOLS
        }
        SOURCES += src/radeprocessor.cpp src/rade_text.c
        HEADERS += include/radeprocessor.h include/rade_text.h
        message("RADE V1 support enabled ($$RADAE_DIR)")
    } else {
        message("RADE V1: radae_nopy found but not built (run: cd $$RADAE_DIR/build && cmake .. && make)")
    }
}

# FreeDV (codec2) support — auto-detect via pkg-config or header presence
linux|macx {
    system(pkg-config --exists codec2) {
        DEFINES += FREEDV_SUPPORT
        LIBS += -lcodec2
        SOURCES += src/freedvprocessor.cpp src/freedvreporter.cpp
        HEADERS += include/freedvprocessor.h include/freedvreporter.h include/spotreporter.h
        message("FreeDV codec2 support enabled")
    } else {
        message("FreeDV codec2 not found — codec2 modes (700D/700E/1600) disabled")
    }
}
win32 {
    !isEmpty(VCPKG_DIR) {
        exists($$VCPKG_DIR/lib/codec2.lib) | exists($$VCPKG_DIR/lib/libcodec2.a) {
            DEFINES += FREEDV_SUPPORT
            LIBS += -lcodec2
            SOURCES += src/freedvprocessor.cpp src/freedvreporter.cpp
            HEADERS += include/freedvprocessor.h include/freedvreporter.h include/spotreporter.h
            message("FreeDV codec2 support enabled")
        } else {
            message("FreeDV codec2 not found — codec2 modes (700D/700E/1600) disabled")
        }
    }
}

# Dire Wolf packet modem (AX.25 / APRS) — opt-in via: qmake CONFIG+=packet
CONFIG(packet) {
    DEFINES += PACKET_SUPPORT
    # Dire Wolf expects these version macros from the build system.
    DEFINES += MAJOR_VERSION=1 MINOR_VERSION=7 EXTRA_VERSION=\\\"wfweb\\\"
    INCLUDEPATH += $$PWD/resources/direwolf/src $$PWD/resources/direwolf
    SOURCES += \
        resources/direwolf/src/ax25_pad.c \
        resources/direwolf/src/ax25_pad2.c \
        resources/direwolf/src/demod.c \
        resources/direwolf/src/demod_afsk.c \
        resources/direwolf/src/demod_9600.c \
        resources/direwolf/src/multi_modem.c \
        resources/direwolf/src/hdlc_rec.c \
        resources/direwolf/src/hdlc_rec2.c \
        resources/direwolf/src/hdlc_send.c \
        resources/direwolf/src/gen_tone.c \
        resources/direwolf/src/fcs_calc.c \
        resources/direwolf/src/dsp.c \
        resources/direwolf/src/dtime_now.c \
        resources/direwolf/src/rrbb.c \
        resources/direwolf/src/ax25_link.c \
        resources/direwolf/src/xid.c \
        resources/direwolf/src/dlq.c \
        resources/direwolf/wfweb_direwolf_stubs.c \
        resources/direwolf/wfweb_dw_server_shim.c \
        resources/direwolf/wfweb_tq.c
    SOURCES += src/direwolfprocessor.cpp src/ax25linkprocessor.cpp
    HEADERS += include/direwolfprocessor.h include/ax25linkprocessor.h
    message("Packet (Dire Wolf) support enabled")
}

contains(DEFINES,FTDI_SUPPORT){
  win32:INCLUDEPATH += ../LibFT4222-v1.4.7\imports\LibFT4222\inc
  win32:INCLUDEPATH += ../LibFT4222-v1.4.7\imports\ftd2xx
}

# vcpkg integration for Windows builds.
win32 {
    isEmpty(VCPKG_DIR): VCPKG_DIR = $$(VCPKG_DIR)
    !isEmpty(VCPKG_DIR) {
        INCLUDEPATH += $$VCPKG_DIR/include
        INCLUDEPATH += $$VCPKG_DIR/include/eigen3
        INCLUDEPATH += $$VCPKG_DIR/include/opus
        INCLUDEPATH += $$VCPKG_DIR/include/hidapi
        LIBS += -L$$VCPKG_DIR/lib
        contains(DEFINES, RADE_SUPPORT) {
            LIBS += -lportaudio -lhidapi -llibssl -llibcrypto
        } else {
            LIBS += -lportaudio -lopus -lhidapi -llibssl -llibcrypto
        }
    } else {
        INCLUDEPATH += ../opus/include
    }
}
!win32:!linux:INCLUDEPATH += ../opus/include
!linux:INCLUDEPATH += ../eigen
!linux:INCLUDEPATH += ../r8brain-free-src

INCLUDEPATH += include
INCLUDEPATH += src/audio
INCLUDEPATH += src/audio/resampler

SOURCES += \
    src/audio/adpcm/adpcm-dns.c \
    src/audio/adpcm/adpcm-lib.c \
    src/audio/audioconverter.cpp \
    src/audio/audiodevices.cpp \
    src/audio/audiohandlerbase.cpp \
    src/audio/audiohandlerpainput.cpp \
    src/audio/audiohandlerpaoutput.cpp \
    src/audio/audiohandlerqtinput.cpp \
    src/audio/audiohandlerqtoutput.cpp \
    src/audio/audiohandlerrtinput.cpp \
    src/audio/audiohandlerrtoutput.cpp \
    src/audio/audiohandlertciinput.cpp \
    src/audio/audiohandlertcioutput.cpp \
    src/audio/resampler/resample.c \
    src/radio/icomcommander.cpp \
    src/radio/icomserver.cpp \
    src/radio/icomudpaudio.cpp \
    src/radio/icomudpbase.cpp \
    src/radio/icomudpcivdata.cpp \
    src/radio/icomudphandler.cpp \
    src/radio/kenwoodcommander.cpp \
    src/radio/kenwoodserver.cpp \
    src/radio/yaesucommander.cpp \
    src/radio/yaesuserver.cpp \
    src/radio/yaesuudpaudio.cpp \
    src/radio/yaesuudpbase.cpp \
    src/radio/yaesuudpcat.cpp \
    src/radio/yaesuudpcontrol.cpp \
    src/radio/yaesuudpscope.cpp \
    src/cachingqueue.cpp \
    src/main.cpp\
    src/servermain.cpp \
    src/commhandler.cpp \
    src/rigcommander.cpp \
    src/rigidentities.cpp \
    src/logcategories.cpp \
    src/pttyhandler.cpp \
    src/tcpserver.cpp \
    src/keyboard.cpp \
    src/rigserver.cpp \
    src/ft4222handler.cpp \
    src/rtpaudio.cpp \
    src/webserver.cpp

macx:SOURCES += src/tlsproxy.cpp


HEADERS  += \
    include/servermain.h \
    src/audio/adpcm/adpcm-lib.h \
    src/audio/resampler/resample_neon.h \
    src/audio/resampler/speex_resampler.h \
    src/audio/resampler/arch.h \
    src/audio/resampler/resample_sse.h \
    include/audioconverter.h \
    include/audiodevices.h \
    include/audiohandler.h \
    include/audiohandlerbase.h \
    include/audiohandlerpainput.h \
    include/audiohandlerpaoutput.h \
    include/audiohandlerqtinput.h \
    include/audiohandlerqtoutput.h \
    include/audiohandlerrtinput.h \
    include/audiohandlerrtoutput.h \
    include/audiohandlertciinput.h \
    include/audiohandlertcioutput.h \
    include/cachingqueue.h \
    include/commhandler.h \
    include/ft4222handler.h \
    include/kenwoodcommander.h \
    include/rigcommander.h \
    include/icomcommander.h \
    include/icomserver.h \
    include/freqmemory.h \
    include/rigidentities.h \
    include/rtpaudio.h \
    include/logcategories.h \
    include/rigserver.h \
    include/packettypes.h \
    include/tcpserver.h \
    include/audiotaper.h \
    include/keyboard.h \
    include/wfwebtypes.h \
    include/pttyhandler.h \
    include/icomudpaudio.h \
    include/icomudpbase.h \
    include/icomudpcivdata.h \
    include/icomudphandler.h \
    include/kenwoodserver.h \
    include/yaesucommander.h \
    include/yaesuserver.h \
    include/yaesuudpaudio.h \
    include/yaesuudpbase.h \
    include/yaesuudpcat.h \
    include/yaesuudpcontrol.h \
    include/yaesuudpscope.h \
    include/webserver.h

macx:HEADERS += include/tlsproxy.h
