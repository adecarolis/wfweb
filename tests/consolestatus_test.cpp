// Unit test for the terminal status page layout (include/consolestatus.h):
// ConsoleStatus::render() must fit any terminal size and keep the URL on
// screen down to a single row.  Built and run by tests/test_cpp_units.py.
#include "consolestatus.h"

#include <QDebug>
#include <cstdlib>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) { qCritical() << "FAIL:" << message; ::exit(1); }
}

ConsoleSnapshot fullSnapshot()
{
    ConsoleSnapshot s;
    s.version = "9.9.9";
    s.name = "shack";
    s.uptimeSecs = 2 * 86400 + 3723;
    s.web = ConsoleSnapshot::WebListening;
    s.webPort = 8080;
    s.https = true;
    s.urls = QStringList{"https://192.168.1.42:8080/", "https://10.0.0.7:8080/", "https://shack-pi:8080/"};
    s.restUrl = "http://192.168.1.42:8081/";
    s.rigctld = "127.0.0.1:4532";
    s.browsers = 2;
    s.model = "IC-7300";
    s.transport = "/dev/ttyUSB0 @ 115200";
    s.rigConnected = true;
    s.frequency = "14.074.000";
    s.mode = "USB";
    s.logFile = "/tmp/wfweb-20261004101500.log";
    s.lastWarning = "10:15:02 No local audio devices found for IC-7300";
    return s;
}

bool contains(const QStringList &lines, const QString &needle)
{
    for (const QString &line : lines)
        if (line.contains(needle)) return true;
    return false;
}
}

int main()
{
    const ConsoleSnapshot full = fullSnapshot();

    // --- fits every terminal size: never taller or wider than asked ---
    const int sizes[][2] = {{200, 60}, {80, 24}, {80, 10}, {40, 10}, {26, 6}, {20, 3}, {10, 1}, {1, 1}};
    for (const auto &size : sizes) {
        const int cols = size[0], rows = size[1];
        const QStringList lines = ConsoleStatus::render(full, cols, rows);
        require(!lines.isEmpty(), "something is drawn at every size");
        require(lines.size() <= rows, "never more lines than rows");
        for (const QString &line : lines) {
            require(line.size() <= cols, "no line wider than the terminal");
            for (const QChar c : line)
                require(c.unicode() >= 0x20 && c.unicode() <= 0x7e, "printable ASCII only");
        }
        // The first URL is the last thing to go, whatever the height.
        require(lines.join("\n").contains(QString("https://192.168.1.42:8080/").left(cols)),
                "first URL survives");
    }
    require(ConsoleStatus::render(full, 0, 0).isEmpty(), "zero-sized terminal draws nothing");

    // --- a roomy terminal shows everything ---
    {
        const QStringList lines = ConsoleStatus::render(full, 80, 24);
        require(contains(lines, "wfweb 9.9.9 - IC-7300 [shack]"), "title with model and name tag");
        require(contains(lines, "up 2d 01:02:03"), "uptime");
        require(contains(lines, "https://shack-pi:8080/"), "every URL listed");
        require(contains(lines, "self-signed"), "certificate note with https");
        require(contains(lines, "IC-7300 on /dev/ttyUSB0 @ 115200  CONNECTED"), "rig line");
        require(contains(lines, "14.074.000 USB  RX"), "frequency line");
        require(contains(lines, "2 connected"), "browser count");
        require(contains(lines, "http://192.168.1.42:8081/"), "REST URL");
        require(contains(lines, "127.0.0.1:4532"), "rigctld");
        require(contains(lines, "/tmp/wfweb-20261004101500.log"), "log file");
        require(contains(lines, "Last warning  10:15:02"), "last warning");
        require(contains(lines, "[l] live log"), "key hints");
    }

    // --- rows go in priority order: with 4 rows the essentials are left ---
    {
        const QStringList lines = ConsoleStatus::render(full, 80, 4);
        require(lines.size() == 4, "exactly fills a 4-row terminal");
        require(contains(lines, "wfweb 9.9.9"), "title kept");
        require(contains(lines, "Open in a browser"), "heading kept");
        require(contains(lines, "https://192.168.1.42:8080/"), "first URL kept");
        require(contains(lines, "[l] live log"), "key hints kept");
        require(!contains(lines, "Log file"), "log file dropped");
        require(!contains(lines, "https://shack-pi"), "extra URLs dropped");
    }
    {
        const QStringList lines = ConsoleStatus::render(full, 80, 1);
        require(lines.size() == 1 && lines.first().contains("https://192.168.1.42:8080/"),
                "one row is the URL");
    }

    // --- web server states that have no URL to show ---
    {
        ConsoleSnapshot s = full;
        s.web = ConsoleSnapshot::WebFailed;
        QStringList lines = ConsoleStatus::render(s, 80, 24);
        require(contains(lines, "FAILED to listen on port 8080"), "bind failure is spelled out");
        require(!contains(lines, "https://192.168.1.42:8080/"), "no URL for a dead server");
        require(ConsoleStatus::render(s, 80, 1).first().contains("FAILED"), "failure survives to one row");

        s.web = ConsoleSnapshot::WebDisabled;
        s.restUrl.clear();
        lines = ConsoleStatus::render(s, 80, 24);
        require(contains(lines, "Web server disabled"), "--no-web is stated");
        require(!contains(lines, "Browsers"), "no browser count without a web server");

        s = ConsoleSnapshot();
        s.version = "9.9.9";
        lines = ConsoleStatus::render(s, 80, 24);
        require(contains(lines, "Starting..."), "placeholder before the server exists");
        require(!contains(lines, "Rig"), "no rig line before the server exists");
    }

    // --- rig not there yet, transmitting, hostile text ---
    {
        ConsoleSnapshot s = full;
        s.rigConnected = false;
        s.model.clear();
        QStringList lines = ConsoleStatus::render(s, 80, 24);
        require(contains(lines, "/dev/ttyUSB0 @ 115200  waiting for the rig"), "waiting state");
        require(!contains(lines, "Frequency"), "no frequency without a rig");

        s = full;
        s.transmitting = true;
        require(contains(ConsoleStatus::render(s, 80, 24), "14.074.000 USB  TX"), "TX shown");

        s = full;
        s.name = QString::fromUtf8("caf\xc3\xa9\x1b[2J");
        lines = ConsoleStatus::render(s, 80, 24);
        require(contains(lines, "[caf??[2J]"), "non-ASCII and control characters are neutralised");
    }

    return 0;
}
