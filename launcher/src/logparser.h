#ifndef LOGPARSER_H
#define LOGPARSER_H

// Reads the wfweb log file the launcher asks for with -l. The server flushes
// that file after every line (messageHandler in src/main.cpp), unlike its
// stdout, which is block-buffered on a pipe. Each line is
//   yyyy-MM-dd hh:mm:ss.zzz LVL category: message
// with LVL one of DBG INF WRN CRT FTL.
//
// classify() maps a message to what it means for the launcher. Only strings
// the server actually logs are recognised; the patterns tolerate the quotes
// and extra spaces QDebug puts around QString arguments.
//
// Qt5Core only, no Q_OBJECT: tests/launcher_test.cpp links this file through
// tests/test_cpp_units.py with nothing but Qt5Core.

#include <QDateTime>
#include <QString>

struct LogLine {
    enum Level { Debug, Info, Warning, Critical, Fatal };
    QDateTime timestamp;
    Level level = Info;
    QString category;   // "default", "system", "serial", "rig", "udp", "audio", "rigctld"
    QString message;
};

struct LogEvent {
    enum Kind {
        Plain,              // nothing the launcher acts on
        WebListening,       // detail = port; https tells the scheme
        WebFailed,          // detail = port (bind failed; the server keeps running)
        WebPlainHttp,       // SSL unavailable, plain HTTP in use
        SerialOpened,       // detail = port name
        SerialFailed,       // detail = port name
        SerialAutoDetected, // detail = "COM5 (IC-7300 ...)"
        SerialFallback,     // no Icom port found, OS default in use
        RigModel,           // detail = model name
        LanLoginFailed,
        LanFailed,          // detail = reason
        AudioWarning,       // detail = message
        Note                // detail = message (CLI override, settings path)
    };
    Kind kind = Plain;
    QString detail;
    bool https = false;
};

namespace LogParser {

// False when the line is not a log record (a continuation of a multi-line
// message, or an empty line).
bool parse(const QString &line, LogLine &out);

LogEvent classify(const QString &message);

// Strips the "..." QDebug wraps around a QString and squeezes the double
// spaces it leaves between arguments.
QString tidy(const QString &message);

} // namespace LogParser

#endif // LOGPARSER_H
