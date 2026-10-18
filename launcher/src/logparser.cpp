#include "logparser.h"

#include <QRegularExpression>

namespace {

const QRegularExpression &lineRe()
{
    static const QRegularExpression re(
        "^(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}) (DBG|INF|WRN|CRT|FTL) ([^:\\s]+): ?(.*)$");
    return re;
}

LogLine::Level levelFor(const QString &tag)
{
    if (tag == "DBG") return LogLine::Debug;
    if (tag == "WRN") return LogLine::Warning;
    if (tag == "CRT") return LogLine::Critical;
    if (tag == "FTL") return LogLine::Fatal;
    return LogLine::Info;
}

QString capture(const QString &text, const char *pattern, int group = 1)
{
    const QRegularExpressionMatch m = QRegularExpression(pattern).match(text);
    return m.hasMatch() ? m.captured(group).trimmed() : QString();
}

LogEvent event(LogEvent::Kind kind, const QString &detail = QString(), bool https = false)
{
    LogEvent e;
    e.kind = kind;
    e.detail = detail;
    e.https = https;
    return e;
}

} // namespace

namespace LogParser {

bool parse(const QString &line, LogLine &out)
{
    const QRegularExpressionMatch m = lineRe().match(line);
    if (!m.hasMatch())
        return false;
    out.timestamp = QDateTime::fromString(m.captured(1), "yyyy-MM-dd hh:mm:ss.zzz");
    out.level = levelFor(m.captured(2));
    out.category = m.captured(3);
    out.message = m.captured(4);
    return true;
}

QString tidy(const QString &message)
{
    QString s = message.trimmed();
    if (s.size() >= 2 && s.startsWith('"') && s.endsWith('"'))
        s = s.mid(1, s.size() - 2);
    s.replace("\\n", " ");
    s.replace(QRegularExpression("\\s{2,}"), " ");
    return s;
}

LogEvent classify(const QString &message)
{
    const QString s = tidy(message);

    // Web server (src/webserver.cpp, src/servermain.cpp)
    QString port = capture(s, "^Web HTTPS server listening on port (\\d+)");
    if (!port.isEmpty()) return event(LogEvent::WebListening, port, true);
    port = capture(s, "^Web HTTP server listening on port (\\d+)");
    if (!port.isEmpty()) return event(LogEvent::WebListening, port, false);
    port = capture(s, "^Web (?:HTTPS|HTTP|plain HTTP REST|WebSocket) server failed to listen on port (\\d+)");
    if (!port.isEmpty()) return event(LogEvent::WebFailed, port);
    if (s.startsWith("Web: SSL not supported")) return event(LogEvent::WebPlainHttp);

    // Serial port (src/commhandler.cpp, src/servermain.cpp)
    QString name = capture(s, "^Opened port: ?\"?([^\"]+?)\"?$");
    if (!name.isEmpty()) return event(LogEvent::SerialOpened, name);
    name = capture(s, "^Could not open serial port ?\"?([^\"]+?)\"?(?: ?,.*)?$");
    if (!name.isEmpty()) return event(LogEvent::SerialFailed, name);
    {
        const QRegularExpressionMatch m = QRegularExpression(
            "^Icom Serial Port found: ?\"?([^\"\\s]+)\"? S/N ?\"?([^\"]*?)\"?$").match(s);
        if (m.hasMatch()) {
            const QString serial = m.captured(2).trimmed();
            return event(LogEvent::SerialAutoDetected,
                         serial.isEmpty() ? m.captured(1) : QString("%1 (%2)").arg(m.captured(1), serial));
        }
    }
    if (s.startsWith("Could not find an Icom serial port")) return event(LogEvent::SerialFallback);

    // Rig identified (src/radio/icomcommander.cpp and the Kenwood/Yaesu twins)
    name = capture(s, "^Loading Rig: (.+?) version\\b");
    if (!name.isEmpty()) return event(LogEvent::RigModel, name);

    // LAN (src/radio/icomudphandler.cpp)
    if (s.contains("Invalid Username/Password")) return event(LogEvent::LanLoginFailed);
    if (s.contains("Connection failed")) return event(LogEvent::LanFailed, "connection failed");
    if (s.contains("Radio not responding")) return event(LogEvent::LanFailed, "radio not responding");
    if (s.contains("Got radio disconnected")) return event(LogEvent::LanFailed, "radio disconnected");

    // Audio (src/webserver.cpp, src/servermain.cpp)
    if (s.startsWith("Web: No rig audio device found")
        || s.startsWith("No local audio devices found")
        || s.startsWith("Web: Qt Multimedia found no audio input devices")
        || s.startsWith("Web: Failed to start USB audio output"))
        return event(LogEvent::AudioWarning, s);

    if (s.contains("overridden via CLI") || s.startsWith("Loading settings from"))
        return event(LogEvent::Note, s);

    return event(LogEvent::Plain);
}

} // namespace LogParser
