#ifndef LAUNCHCONFIG_H
#define LAUNCHCONFIG_H

// What the launcher form collects, and the one place the wfweb command line
// is composed from it. The launcher never edits the server's settings: every
// field becomes a CLI flag on each start (the server does not persist CLI
// overrides, see applyCLIOverrides in src/servermain.cpp), so an existing
// user's server settings are left exactly as they were.
//
// Qt5Core only, no Q_OBJECT: tests/launcher_test.cpp links this file through
// tests/test_cpp_units.py with nothing but Qt5Core.

#include <QString>
#include <QStringList>

class QSettings;

struct LaunchConfig {
    enum Transport { Usb = 0, Lan = 1 };
    enum Manufacturer { Icom = 0, Kenwood = 1, Yaesu = 2 };

    static const int defaultWebPort = 8080;

    Transport transport = Usb;

    // USB
    QString serialPort;          // "COM5"; empty = server default ("auto")
    Manufacturer manufacturer = Icom;
    QString civAddress;          // "0x94" or "148"; empty = rig default
    QString audioDevice;         // Qt Multimedia device name; empty = server default

    // LAN (Icom only)
    QString lanHost;
    QString lanUser;
    QString lanPassword;
    bool noAutoConnect = false;  // --no-autoconnect (LAN only)

    // Server
    int webPort = defaultWebPort;
    QString nameTag;             // -n
    QString settingsFile;        // -s, absolute; empty = server default location
    QString extraArguments;      // appended verbatim, split like a shell would

    // Launcher behaviour
    bool startServerOnOpen = false;
    bool openBrowserOnStart = true;

    // The wfweb command line for this configuration. logFile is the absolute
    // path the launcher tails, passed as -l on every start.
    QStringList toArguments(const QString &logFile) const;

    // Plain-HTTP REST URLs to probe for readiness, most likely first: the
    // REST port next to HTTPS (webPort+1), then the web port itself, which is
    // where REST lives when the server fell back to plain HTTP.
    QStringList restProbeUrls() const;

    // The address to open in the browser once the server listens.
    QString webUrl(bool https) const;

    void load(const QSettings &s);
    void save(QSettings &s) const;
};

#endif // LAUNCHCONFIG_H
