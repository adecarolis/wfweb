#ifndef CONSOLESTATUS_H
#define CONSOLESTATUS_H

#include <QObject>
#include <QElapsedTimer>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>

// Everything the terminal status page shows. servermain fills the rig and
// web-server fields; ConsoleStatus adds version, uptime, log file and the last
// warning before rendering.
struct ConsoleSnapshot {
    enum WebState { WebStarting, WebListening, WebFailed, WebDisabled };

    QString version;
    QString name;               // --name tag, empty when not set
    qint64 uptimeSecs = 0;

    WebState web = WebStarting;
    quint16 webPort = 0;
    bool https = false;         // self-signed certificate in use
    QStringList urls;           // one per reachable address, best first
    QString restUrl;            // plain-HTTP REST endpoint, empty when none
    bool restFailed = false;
    QString rigctld;            // "127.0.0.1:4532", empty when disabled
    int browsers = 0;

    QString model;              // empty until the rig is identified
    QString transport;          // "/dev/ttyUSB0 @ 115200" or "LAN 192.168.1.50"
    bool rigConnected = false;
    QString frequency;          // "14.074.000", empty when unknown
    QString mode;
    bool transmitting = false;

    QString logFile;
    QString lastWarning;        // "hh:mm:ss text", empty when none
};

// Full-screen status page for an interactive terminal: the URLs to open, the
// rig, the ports, with the log one keypress away. Plain ANSI escapes only (no
// curses), so the same code serves Linux, macOS and Windows 10+ consoles.
//
// Deliberately has no Q_OBJECT: it needs no signals or slots of its own, and
// staying moc-free lets tests/consolestatus_test.cpp link it against Qt5Core
// alone.
class ConsoleStatus : public QObject
{
public:
    explicit ConsoleStatus(const QString &logFile, QObject *parent = nullptr);
    ~ConsoleStatus();

    // True when stdin/stdout are an interactive terminal that can show the page.
    static bool wanted();

    // Lay the page out for a cols x rows terminal: never more than `rows`
    // lines, none wider than `cols`. Rows are dropped by priority when the
    // terminal is short; the first URL survives down to a single row.
    static QStringList render(const ConsoleSnapshot &snap, int cols, int rows);

    // Put the terminal back the way it was found. Idempotent and
    // async-signal-safe, so it can be called from signal handlers and atexit.
    static void restoreTerminal();

    void setProvider(std::function<ConsoleSnapshot()> provider) { provider_ = provider; }

    // Take over the terminal (single-key input, alternate screen) and start
    // refreshing. startInLog shows the live log first instead of the page.
    void enter(bool startInLog);

    // One formatted log line, from any thread. Shown at once in the log view,
    // otherwise kept for the next time the log view is opened.
    void logLine(QtMsgType type, const QString &line);

    // A key typed by the user ('l' toggles between page and log).
    void onKey(char key);

private:
    void paint();
    void showPage();
    void showLog();

    std::function<ConsoleSnapshot()> provider_;
    QString logFile_;
    QTimer timer_;
    QElapsedTimer uptime_;

    QMutex mutex_;              // guards ring_ and lastWarning_
    QStringList ring_;
    QString lastWarning_;
};

#endif // CONSOLESTATUS_H
