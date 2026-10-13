# wfweb launcher — a Qt Widgets window that composes the wfweb command line,
# runs the server as a child process, follows its log and opens the browser.
# Separate from wfweb.pro on purpose: the server binary stays CLI-only.
#
# Build (Windows, from this directory, in a VS + Qt environment):
#   qmake launcher.pro CONFIG+=release && nmake release
# build.bat and the Windows CI job do this after the server and drop the exe
# next to wfweb.exe in wfweb-release/. The Qt modules it uses are the ones
# windeployqt already ships for the server, so no new DLLs.

QT       += core gui widgets network serialport multimedia

TARGET   = wfweb-launcher
TEMPLATE = app

CONFIG   += c++17
CONFIG   -= console
macx:CONFIG -= app_bundle

DEFINES += QT_DEPRECATED_WARNINGS

# Version from include/wfweb_version.h, the single source of truth.
VERSION_LINES = $$cat($$PWD/../include/wfweb_version.h, lines)
VERSION_LINE  = $$find(VERSION_LINES, "define WFWEB_VERSION ")
VERSION       = $$replace(VERSION_LINE, "[^0-9.]", "")
isEmpty(VERSION): error("could not read WFWEB_VERSION from include/wfweb_version.h")

QMAKE_TARGET_PRODUCT     = "wfweb launcher"
QMAKE_TARGET_DESCRIPTION = "Starts and supervises the wfweb server"
QMAKE_TARGET_COMPANY     = "wfweb"

INCLUDEPATH += $$PWD/src $$PWD/../include

CONFIG(debug, release|debug) {
    DESTDIR = $$PWD/../wfweb-debug
} else {
    DESTDIR = $$PWD/../wfweb-release
}

RC_ICONS  = res/wfweb.ico
RESOURCES = res/launcher.qrc

# portowner.cpp: who holds the web port (GetExtendedTcpTable, ntohs).
win32:LIBS += -liphlpapi -lws2_32

SOURCES += \
    src/main.cpp \
    src/launchconfig.cpp \
    src/logparser.cpp \
    src/logtail.cpp \
    src/portowner.cpp \
    src/serverprocess.cpp \
    src/statuspoller.cpp \
    src/mainwindow.cpp

HEADERS += \
    src/launchconfig.h \
    src/logparser.h \
    src/logtail.h \
    src/portowner.h \
    src/serverprocess.h \
    src/statuspoller.h \
    src/mainwindow.h
