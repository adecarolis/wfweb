#include "consolestatus.h"
#include "wfweb_version.h"

#include <QDateTime>
#include <QMutexLocker>

#include <cstdio>
#include <cstdlib>

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <atomic>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004   // older Windows SDKs
#endif
#else
#include <csignal>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

// What the terminal is currently set up for. Written by the main thread and by
// restoreTerminal(), which may run inside a signal handler.
enum TermState { TermNormal, TermLog, TermPage };

const char pageOn[]  = "\x1b[?1049h\x1b[?25l\x1b[?7l";   // alt screen, no cursor, no wrap
const char pageOff[] = "\x1b[?7h\x1b[?25h\x1b[?1049l";
const int ringMax = 500;

#ifdef Q_OS_WIN
std::atomic<int> termState{TermNormal};
DWORD savedInMode = 0;
DWORD savedOutMode = 0;
#else
volatile sig_atomic_t termState = TermNormal;
struct termios savedTermios;
#endif

void put(const QByteArray &bytes)
{
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    fflush(stdout);
}

void termSize(int &cols, int &rows)
{
    cols = 80;
    rows = 24;
#ifdef Q_OS_WIN
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        cols = info.srWindow.Right - info.srWindow.Left + 1;
        rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
#endif
}

// One terminal cell per character: anything outside printable ASCII (a name
// tag or a log message can hold anything) would break the width arithmetic.
QString cells(const QString &text, int cols)
{
    QString out = text.left(cols);
    for (QChar &c : out) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e)
            c = QLatin1Char('?');
    }
    return out;
}

QString uptimeText(qint64 secs)
{
    const qint64 days = secs / 86400;
    const QString clock = QString("%1:%2:%3")
        .arg((secs / 3600) % 24, 2, 10, QLatin1Char('0'))
        .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(secs % 60, 2, 10, QLatin1Char('0'));
    return days > 0 ? QString("up %1d %2").arg(days).arg(clock) : "up " + clock;
}

} // namespace

ConsoleStatus::ConsoleStatus(const QString &logFile, QObject *parent)
    : QObject(parent), logFile_(logFile)
{
    connect(&timer_, &QTimer::timeout, this, [this]() { paint(); });
}

ConsoleStatus::~ConsoleStatus()
{
    timer_.stop();
    restoreTerminal();
}

bool ConsoleStatus::wanted()
{
#ifdef Q_OS_WIN
    if (!_isatty(_fileno(stdin)) || !_isatty(_fileno(stdout)))
        return false;
    // Older consoles cannot interpret escape sequences at all.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(out, &mode))
        return false;
    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return false;
    SetConsoleMode(out, mode);
    return true;
#else
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return false;
    const QByteArray term = qgetenv("TERM");
    return !term.isEmpty() && term != "dumb";
#endif
}

QStringList ConsoleStatus::render(const ConsoleSnapshot &snap, int cols, int rows)
{
    if (cols < 1 || rows < 1)
        return QStringList();

    // In drop order: when the terminal is too short, the earliest kinds go first.
    enum Kind { Blank, CertNote, Warning, LogFile, RigCtl, Rest, Browsers,
                Frequency, ExtraUrl, Rig, Heading, Keys, Title, FirstUrl };
    struct Row { Kind kind; QString text; };
    QList<Row> page;

    const bool wide = cols >= 40;
    const QString margin = wide ? " " : "";
    const QString indent = wide ? "   " : "";
    auto add = [&](Kind kind, const QString &text) { page.append({kind, margin + text}); };
    auto field = [&](Kind kind, const char *label, const QString &text) {
        add(kind, QString("%1 %2").arg(QLatin1String(label), -10).arg(text));
    };

    QString title = "wfweb " + snap.version;
    if (!snap.model.isEmpty())
        title += " - " + snap.model;
    if (!snap.name.isEmpty())
        title += " [" + snap.name + "]";
    const QString up = uptimeText(snap.uptimeSecs);
    const int width = qMin(cols, 72) - margin.size();
    if (title.size() + 2 + up.size() <= width)
        title += QString(width - title.size() - up.size(), QLatin1Char(' ')) + up;
    add(Title, title);
    add(Blank, QString());

    switch (snap.web) {
    case ConsoleSnapshot::WebListening:
        add(Heading, "Open in a browser:");
        for (int i = 0; i < snap.urls.size(); i++)
            page.append({i == 0 ? FirstUrl : ExtraUrl, indent + snap.urls.at(i)});
        if (snap.https)
            page.append({CertNote, indent + "(self-signed certificate: accept the browser warning once)"});
        break;
    case ConsoleSnapshot::WebStarting:
        add(FirstUrl, snap.webPort ? QString("Web server starting on port %1...").arg(snap.webPort)
                                   : QString("Starting..."));
        break;
    case ConsoleSnapshot::WebFailed:
        add(FirstUrl, QString("Web server FAILED to listen on port %1 (already in use?)").arg(snap.webPort));
        break;
    case ConsoleSnapshot::WebDisabled:
        add(FirstUrl, "Web server disabled (--no-web)");
        break;
    }
    add(Blank, QString());

    const QString rig = snap.model.isEmpty() ? snap.transport : snap.model + " on " + snap.transport;
    field(Rig, "Rig", rig + (snap.rigConnected ? "  CONNECTED" : "  waiting for the rig"));
    if (snap.rigConnected && !snap.frequency.isEmpty())
        field(Frequency, "Frequency", snap.frequency + " " + snap.mode + (snap.transmitting ? "  TX" : "  RX"));
    if (snap.web == ConsoleSnapshot::WebListening)
        field(Browsers, "Browsers", snap.browsers > 0 ? QString("%1 connected").arg(snap.browsers) : "none connected");
    if (!snap.restUrl.isEmpty())
        field(Rest, "REST API", snap.restFailed ? "FAILED to listen (" + snap.restUrl + ")" : snap.restUrl);
    if (!snap.rigctld.isEmpty())
        field(RigCtl, "rigctld", snap.rigctld);
    if (!snap.logFile.isEmpty())
        field(LogFile, "Log file", snap.logFile);
    if (!snap.lastWarning.isEmpty()) {
        add(Blank, QString());
        add(Warning, "Last warning  " + snap.lastWarning);
    }
    add(Blank, QString());
    add(Keys, "[l] live log   [q] quit");

    while (page.size() > rows) {
        int victim = 0;
        for (int i = 1; i < page.size(); i++) {
            if (page.at(i).kind <= page.at(victim).kind)
                victim = i;
        }
        page.removeAt(victim);
    }

    QStringList lines;
    for (const Row &row : page)
        lines.append(cells(row.text, cols));
    return lines;
}

void ConsoleStatus::restoreTerminal()
{
    const int state = termState;
    if (state == TermNormal)
        return;
    termState = TermNormal;
#ifdef Q_OS_WIN
    if (state == TermPage) {
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), pageOff, sizeof(pageOff) - 1, &written, NULL);
    }
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), savedInMode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), savedOutMode);
#else
    if (state == TermPage) {
        ssize_t ignored = write(STDOUT_FILENO, pageOff, sizeof(pageOff) - 1);
        (void)ignored;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &savedTermios);
#endif
}

void ConsoleStatus::enter(bool startInLog)
{
    if (termState != TermNormal)
        return;

    // Keys arrive one at a time and are not echoed. Signal keys stay live so
    // Ctrl-C still quits; suspend is switched off because a stopped job would
    // leave the shell in this mode.
#ifdef Q_OS_WIN
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(in, &savedInMode);
    GetConsoleMode(out, &savedOutMode);
    SetConsoleMode(in, savedInMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    SetConsoleMode(out, savedOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    tcgetattr(STDIN_FILENO, &savedTermios);
    struct termios raw = savedTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    raw.c_cc[VSUSP] = _POSIX_VDISABLE;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif
    termState = TermLog;
    atexit(restoreTerminal);

    uptime_.start();
    timer_.start(1000);
    if (startInLog)
        put("--- live log: press l for the status page, q to quit ---\n");
    else
        showPage();
}

void ConsoleStatus::logLine(QtMsgType type, const QString &msg)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString line = now.toString("yyyy-MM-dd hh:mm:ss.zzz ") + msg;

    QMutexLocker locker(&mutex_);
    ring_.append(line);
    if (ring_.size() > ringMax)
        ring_.removeFirst();
    if (type == QtWarningMsg || type == QtCriticalMsg)
        lastWarning_ = now.toString("hh:mm:ss ") + msg;
    if (termState != TermPage)
        put(line.toLocal8Bit() + "\n");
}

void ConsoleStatus::onKey(char key)
{
    if (key != 'l' && key != 'L')
        return;
    if (termState == TermPage)
        showLog();
    else if (termState == TermLog)
        showPage();
}

void ConsoleStatus::showPage()
{
    {
        QMutexLocker locker(&mutex_);
        put(pageOn);
        termState = TermPage;
    }
    paint();
}

void ConsoleStatus::showLog()
{
    // Back on the normal screen, so the terminal's own scrollback and
    // copy/paste work on the log.
    QMutexLocker locker(&mutex_);
    put(pageOff);
    termState = TermLog;
    QByteArray backlog;
    for (const QString &line : ring_)
        backlog += line.toLocal8Bit() + "\n";
    backlog += "--- live log: press l for the status page, q to quit ---\n";
    put(backlog);
}

void ConsoleStatus::paint()
{
    if (termState != TermPage)
        return;

    // The provider may log, which takes mutex_ through logLine(): call it
    // before locking.
    ConsoleSnapshot snap = provider_ ? provider_() : ConsoleSnapshot();
    snap.version = QString(WFWEB_VERSION);
    snap.uptimeSecs = uptime_.elapsed() / 1000;
    snap.logFile = logFile_;
    {
        QMutexLocker locker(&mutex_);
        snap.lastWarning = lastWarning_;
    }

    int cols, rows;
    termSize(cols, rows);
    const QStringList lines = render(snap, cols, rows);

    // Every row is repainted in place each tick: a resize or a stray write to
    // the terminal from a library corrects itself within a second.
    QByteArray frame;
    for (int i = 0; i < lines.size(); i++)
        frame += "\x1b[" + QByteArray::number(i + 1) + ";1H" + lines.at(i).toLatin1() + "\x1b[K";
    if (lines.size() < rows)
        frame += "\x1b[" + QByteArray::number(lines.size() + 1) + ";1H\x1b[J";
    if (termState == TermPage)
        put(frame);
}
