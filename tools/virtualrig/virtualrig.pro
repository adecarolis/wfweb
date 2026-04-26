# Virtual rig simulator — standalone Qt app reusing wfweb's icomServer.

QT       += core serialport network multimedia websockets
CONFIG   += console c++17
CONFIG   -= app_bundle

TARGET   = virtualrig
TEMPLATE = app

WFWEB_ROOT = $$PWD/../..

# Same preprocessor knobs as wfweb proper (so shared headers compile identically).
DEFINES += WFWEB_VERSION=\\\"vrig\\\"
DEFINES += BUILD_WFSERVER
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += OUTSIDE_SPEEX
DEFINES += RANDOM_PREFIX=wf
DEFINES += EIGEN_MPL2_ONLY
DEFINES += EIGEN_DONT_VECTORIZE
equals(QT_ARCH, x86_64): DEFINES += EIGEN_VECTORIZE_SSE3

linux:DEFINES += __LINUX_PULSE__
linux:QMAKE_CXXFLAGS += -faligned-new
linux:LIBS += -lpulse -lpulse-simple -lrtaudio -lpthread -L$$WFWEB_ROOT -lopus -lportaudio

INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$WFWEB_ROOT/include
INCLUDEPATH += $$WFWEB_ROOT/src/audio
INCLUDEPATH += $$WFWEB_ROOT/src/audio/resampler

# Headers pulled through transitive includes expect these prefixes.
macx:DEFINES += __MACOSX_CORE__
macx:INCLUDEPATH += /usr/local/include /opt/local/include /opt/homebrew/include

# Sibling checkouts matching wfweb.pro layout
!win32:!linux:INCLUDEPATH += $$WFWEB_ROOT/../opus/include
!linux:INCLUDEPATH += $$WFWEB_ROOT/../rtaudio
!linux:INCLUDEPATH += $$WFWEB_ROOT/../eigen
!linux:INCLUDEPATH += $$WFWEB_ROOT/../r8brain-free-src

macx:LIBS += -L/usr/local/lib -L/opt/local/lib -L/opt/homebrew/lib \
    -framework CoreAudio -framework CoreFoundation \
    -lpthread -lopus -lportaudio

SOURCES += \
    src/main.cpp \
    src/virtualrig.cpp \
    src/civemulator.cpp \
    src/channelmixer.cpp \
    src/controlserver.cpp

HEADERS += \
    src/virtualrig.h \
    src/civemulator.h \
    src/channelmixer.h \
    src/controlserver.h

# Sources reused from wfweb. Kept deliberately tight: just the server stack
# + audio-packet utilities + rig plumbing. No webserver, no GUI.
# RtAudio source (non-linux builds use the sibling checkout)
!linux:SOURCES += $$WFWEB_ROOT/../rtaudio/RTAudio.cpp
!linux:HEADERS += $$WFWEB_ROOT/../rtaudio/RTAudio.h

SOURCES += \
    $$WFWEB_ROOT/src/audio/adpcm/adpcm-dns.c \
    $$WFWEB_ROOT/src/audio/adpcm/adpcm-lib.c \
    $$WFWEB_ROOT/src/audio/resampler/resample.c \
    $$WFWEB_ROOT/src/audio/audioconverter.cpp \
    $$WFWEB_ROOT/src/audio/audiodevices.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlerbase.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlerpainput.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlerpaoutput.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlerqtinput.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlerqtoutput.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlerrtinput.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlerrtoutput.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlertciinput.cpp \
    $$WFWEB_ROOT/src/audio/audiohandlertcioutput.cpp \
    $$WFWEB_ROOT/src/radio/icomcommander.cpp \
    $$WFWEB_ROOT/src/radio/icomserver.cpp \
    $$WFWEB_ROOT/src/radio/icomudpaudio.cpp \
    $$WFWEB_ROOT/src/radio/icomudpbase.cpp \
    $$WFWEB_ROOT/src/radio/icomudpcivdata.cpp \
    $$WFWEB_ROOT/src/radio/icomudphandler.cpp \
    $$WFWEB_ROOT/src/cachingqueue.cpp \
    $$WFWEB_ROOT/src/commhandler.cpp \
    $$WFWEB_ROOT/src/rigcommander.cpp \
    $$WFWEB_ROOT/src/rigidentities.cpp \
    $$WFWEB_ROOT/src/logcategories.cpp \
    $$WFWEB_ROOT/src/pttyhandler.cpp \
    $$WFWEB_ROOT/src/tcpserver.cpp \
    $$WFWEB_ROOT/src/rigserver.cpp \
    $$WFWEB_ROOT/src/rtpaudio.cpp

HEADERS += \
    $$WFWEB_ROOT/include/audioconverter.h \
    $$WFWEB_ROOT/include/audiodevices.h \
    $$WFWEB_ROOT/include/audiohandlerbase.h \
    $$WFWEB_ROOT/include/audiohandlerpainput.h \
    $$WFWEB_ROOT/include/audiohandlerpaoutput.h \
    $$WFWEB_ROOT/include/audiohandlerqtinput.h \
    $$WFWEB_ROOT/include/audiohandlerqtoutput.h \
    $$WFWEB_ROOT/include/audiohandlerrtinput.h \
    $$WFWEB_ROOT/include/audiohandlerrtoutput.h \
    $$WFWEB_ROOT/include/audiohandlertciinput.h \
    $$WFWEB_ROOT/include/audiohandlertcioutput.h \
    $$WFWEB_ROOT/include/cachingqueue.h \
    $$WFWEB_ROOT/include/commhandler.h \
    $$WFWEB_ROOT/include/rigcommander.h \
    $$WFWEB_ROOT/include/icomcommander.h \
    $$WFWEB_ROOT/include/icomserver.h \
    $$WFWEB_ROOT/include/icomudpaudio.h \
    $$WFWEB_ROOT/include/icomudpbase.h \
    $$WFWEB_ROOT/include/icomudpcivdata.h \
    $$WFWEB_ROOT/include/icomudphandler.h \
    $$WFWEB_ROOT/include/packettypes.h \
    $$WFWEB_ROOT/include/pttyhandler.h \
    $$WFWEB_ROOT/include/rigidentities.h \
    $$WFWEB_ROOT/include/rigserver.h \
    $$WFWEB_ROOT/include/rtpaudio.h \
    $$WFWEB_ROOT/include/tcpserver.h \
    $$WFWEB_ROOT/include/wfwebtypes.h
