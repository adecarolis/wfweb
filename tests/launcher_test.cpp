// Unit test for the Windows launcher's pure helpers (launcher/src):
// LaunchConfig::toArguments() must produce exactly the wfweb command line a
// user would type, and LogParser must recognise the strings the server
// really logs. Built and run by tests/test_cpp_units.py (Qt5Core only).
#include "../launcher/src/launchconfig.h"
#include "../launcher/src/logparser.h"
#include "../launcher/src/portowner.h"

#include <QDebug>
#include <cstdlib>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) { qCritical() << "FAIL:" << message; ::exit(1); }
}

int indexOfFlag(const QStringList &args, const QString &flag)
{
    return args.indexOf(flag);
}

// The value following a flag, or an empty string when the flag is absent.
QString valueOf(const QStringList &args, const QString &flag)
{
    const int i = indexOfFlag(args, flag);
    return (i >= 0 && i + 1 < args.size()) ? args.at(i + 1) : QString();
}

LogEvent classified(const char *message)
{
    return LogParser::classify(QString::fromUtf8(message));
}
}

int main()
{
    const QString log = "C:/Users/me/AppData/Local/wfweb/launcher/wfweb-server.log";

    // --- toArguments -----------------------------------------------------

    // USB with everything at its default: only the launcher's own flags.
    {
        LaunchConfig c;
        const QStringList args = c.toArguments(log);
        require(args == QStringList{"--no-tui", "-l", log},
                "USB/auto with defaults adds nothing beyond --no-tui and -l");
    }

    // USB with an explicit port, manufacturer, CI-V address and audio device.
    {
        LaunchConfig c;
        c.serialPort = "COM7";
        c.manufacturer = LaunchConfig::Kenwood;
        c.civAddress = " 0x94 ";
        c.audioDevice = "USB Audio CODEC";
        const QStringList args = c.toArguments(log);
        require(valueOf(args, "--serial-port") == "COM7", "--serial-port carries the port");
        require(valueOf(args, "--manufacturer") == "1", "Kenwood is manufacturer 1");
        require(valueOf(args, "--civ") == "0x94", "--civ is trimmed");
        require(valueOf(args, "--audio-device") == "USB Audio CODEC", "device name stays one argument");
        require(indexOfFlag(args, "--lan") < 0, "no LAN flags in USB mode");
        require(indexOfFlag(args, "-p") < 0, "default web port needs no -p");
    }

    // Icom is the server default: no --manufacturer.
    {
        LaunchConfig c;
        c.manufacturer = LaunchConfig::Icom;
        require(indexOfFlag(c.toArguments(log), "--manufacturer") < 0, "Icom adds no --manufacturer");
    }

    // LAN: host and credentials, and the USB fields are ignored even if set.
    {
        LaunchConfig c;
        c.transport = LaunchConfig::Lan;
        c.lanHost = "192.168.1.50 ";
        c.lanUser = "alain";
        c.lanPassword = "secret";
        c.noAutoConnect = true;
        c.serialPort = "COM7";
        c.audioDevice = "USB Audio CODEC";
        const QStringList args = c.toArguments(log);
        require(valueOf(args, "--lan") == "192.168.1.50", "--lan carries the trimmed host");
        require(valueOf(args, "--lan-user") == "alain", "--lan-user");
        require(valueOf(args, "--lan-pass") == "secret", "--lan-pass");
        require(indexOfFlag(args, "--no-autoconnect") >= 0, "--no-autoconnect in LAN mode");
        require(indexOfFlag(args, "--serial-port") < 0, "no serial flag in LAN mode");
        require(indexOfFlag(args, "--audio-device") < 0, "no audio flag in LAN mode");
    }

    // --no-autoconnect is LAN only.
    {
        LaunchConfig c;
        c.noAutoConnect = true;
        require(indexOfFlag(c.toArguments(log), "--no-autoconnect") < 0, "--no-autoconnect ignored for USB");
    }

    // Server fields and extra arguments.
    {
        LaunchConfig c;
        c.webPort = 8090;
        c.nameTag = "Shack";
        c.settingsFile = "C:/Users/me/wfweb/contest.ini";
        c.extraArguments = "--rigctld-port 4532 --remote-log \"127.0.0.1:2237\"";
        const QStringList args = c.toArguments(log);
        require(valueOf(args, "-p") == "8090", "-p for a non-default port");
        require(valueOf(args, "-n") == "Shack", "-n carries the name tag");
        require(valueOf(args, "-s") == "C:/Users/me/wfweb/contest.ini", "-s carries the settings file");
        require(valueOf(args, "--rigctld-port") == "4532", "extra arguments are appended");
        require(valueOf(args, "--remote-log") == "127.0.0.1:2237", "quoted extra arguments are unquoted");
        require(args.last() == "127.0.0.1:2237", "extra arguments come last");
    }

    // Probe URLs and browser URL.
    {
        LaunchConfig c;
        require(c.restProbeUrls() == QStringList{"http://127.0.0.1:8081/api/v1/radio/info",
                                                 "http://127.0.0.1:8080/api/v1/radio/info"},
                "REST probe tries the HTTPS-companion port first, then the web port");
        require(c.webUrl(true) == "https://localhost:8080/", "https browser URL");
        require(c.webUrl(false) == "http://localhost:8080/", "http browser URL");
    }

    // --- LogParser::parse ------------------------------------------------

    {
        LogLine l;
        require(LogParser::parse("2026-10-13 12:01:03.123 INF default: Web HTTPS server listening on port 8080", l),
                "a log record parses");
        require(l.level == LogLine::Info, "INF is Info");
        require(l.category == "default", "category before the colon");
        require(l.message == "Web HTTPS server listening on port 8080", "message after the colon");
        require(l.timestamp.isValid() && l.timestamp.time().msec() == 123, "timestamp with milliseconds");

        require(LogParser::parse("2026-10-13 12:01:03.124 WRN system: Could not find an Icom serial port.", l),
                "warning record parses");
        require(l.level == LogLine::Warning && l.category == "system", "WRN system");

        require(!LogParser::parse("Operating System: Windows 11 Version 24H2", l), "a continuation line is not a record");
        require(!LogParser::parse("", l), "an empty line is not a record");
    }

    // --- LogParser::tidy / classify -------------------------------------

    require(LogParser::tidy("\"Have new rig ID: 0x94\"") == "Have new rig ID: 0x94", "tidy strips QDebug quotes");
    require(LogParser::tidy("Opened port:  \"COM4\"") == "Opened port: \"COM4\"", "tidy squeezes double spaces");

    {
        LogEvent e = classified("Web HTTPS server listening on port 8080");
        require(e.kind == LogEvent::WebListening && e.https && e.detail == "8080", "HTTPS listening");
        e = classified("Web HTTP server listening on port 8080");
        require(e.kind == LogEvent::WebListening && !e.https && e.detail == "8080", "HTTP listening");
        e = classified("Web plain HTTP REST server listening on port 8081 (use http:// on this port for scripts/microcontrollers)");
        require(e.kind == LogEvent::Plain, "the REST listening line is not the web listening line");
        e = classified("Web HTTPS server failed to listen on port 8080");
        require(e.kind == LogEvent::WebFailed && e.detail == "8080", "HTTPS bind failure");
        e = classified("Web plain HTTP REST server failed to listen on port 8081");
        require(e.kind == LogEvent::WebFailed && e.detail == "8081", "REST bind failure");
        e = classified("Web: SSL not supported on this system, using plain HTTP");
        require(e.kind == LogEvent::WebPlainHttp, "plain HTTP fallback");

        e = classified("Opened port:  \"COM4\"");
        require(e.kind == LogEvent::SerialOpened && e.detail == "COM4", "serial opened");
        e = classified("Could not open serial port  \"COM4\"  , please check Radio Access under Settings.");
        require(e.kind == LogEvent::SerialFailed && e.detail == "COM4", "serial open failure");
        e = classified("Icom Serial Port found:  \"COM4\" S/N \"IC-7300 03001234\"");
        require(e.kind == LogEvent::SerialAutoDetected && e.detail == "COM4 (IC-7300 03001234)", "serial auto-detect");
        e = classified("Could not find an Icom serial port. Falling back to OS default. Use --serial-port to specify, or modify preferences.");
        require(e.kind == LogEvent::SerialFallback, "serial fallback");

        e = classified("\"Loading Rig: IC-7300 version 2.00 from C:/wfweb/rigs/IC-7300.rig\"");
        require(e.kind == LogEvent::RigModel && e.detail == "IC-7300", "rig model from the quoted Loading Rig line");
        e = classified("\"Loading Rig: IC-9700 version 1.10 from /usr/share/wfview/rigs/IC-9700.rig\"");
        require(e.detail == "IC-9700", "rig model with a different name");

        e = classified("icomUdpHandler : Invalid Username/Password");
        require(e.kind == LogEvent::LanLoginFailed, "LAN login failure");
        e = classified("icomUdpHandler : Radio not responding.");
        require(e.kind == LogEvent::LanFailed && e.detail == "radio not responding", "LAN not responding");
        e = classified("icomUdpHandler : Connection failed, wait a few minutes or reboot the radio.");
        require(e.kind == LogEvent::LanFailed, "LAN connection failed");
        e = classified("icomUdpHandler : Got radio disconnected.");
        require(e.kind == LogEvent::LanFailed, "LAN disconnected");

        e = classified("Web: No rig audio device found for direct capture");
        require(e.kind == LogEvent::AudioWarning, "audio warning (web capture)");
        e = classified("No local audio devices found for \"IC-7300\" - skipping local audio setup (LAN audio unaffected)");
        require(e.kind == LogEvent::AudioWarning, "audio warning (no local devices)");

        e = classified("Serial port overridden via CLI: \"COM4\"");
        require(e.kind == LogEvent::Note, "CLI override note");
        e = classified("Loading settings from: \"C:/Users/me/wfweb/contest.ini\"");
        require(e.kind == LogEvent::Note, "settings path note");

        e = classified("Auto Information enabled for meters");
        require(e.kind == LogEvent::Plain, "an unrelated line is plain");
        e = classified("\"wfweb version: 0.9.2 (Git:7b3b6a6 on Oct 13 2026 at 10:00:00 by build@wfweb.org)\\nOperating System: Windows 11\"");
        require(e.kind == LogEvent::Plain, "the version banner is plain");
    }

    // --- PortOwner::classify / masked ------------------------------------

    {
        const QString logPath = "C:/Users/me/AppData/Local/wfweb/launcher/wfweb-server.log";
        PortOwner o;
        o.image = "wfweb.exe";
        o.commandLine = "C:\\wfweb\\wfweb.exe --no-tui -l C:\\Users\\me\\AppData\\Local\\wfweb\\launcher\\wfweb-server.log "
                        "--lan 192.168.1.5 --lan-user u --lan-pass secret";
        o.classify(logPath);
        require(o.isWfweb, "wfweb.exe is wfweb");
        require(o.isLan, "a bare --lan is a LAN instance");
        require(o.fromLauncher, "the launcher's -l path is recognised across slash styles");
        const QString masked = PortOwner::masked(o.commandLine);
        require(!masked.contains("secret") && masked.contains("--lan-pass ****"), "the LAN password is masked");
        require(PortOwner::masked("wfweb --lan-pass \"two words\" -p 8090").contains("--lan-pass **** -p"),
                "a quoted password is masked whole");

        PortOwner console;
        console.image = "WFWEB.EXE";
        console.commandLine = "wfweb.exe --debug x.log --lan-user u";
        console.classify(logPath);
        require(console.isWfweb, "image name compares case-insensitively");
        require(!console.isLan, "--lan-user alone is not --lan");
        require(!console.fromLauncher, "no -l path means not started by the launcher");

        PortOwner other;
        other.image = "node.exe";
        other.commandLine = "node server.js";
        other.classify(logPath);
        require(!other.isWfweb, "another program is not wfweb");
        require(PortOwner::masked("no password here") == "no password here", "masking leaves other lines alone");
    }

    qInfo() << "launcher_test: all checks passed";
    return 0;
}
