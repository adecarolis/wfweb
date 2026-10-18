#ifndef PORTOWNER_H
#define PORTOWNER_H

// Who is listening on a local TCP port, so a "port in use" refusal can say
// whether it is another wfweb and offer to stop it.
//
// lookup() and terminate() are Windows-only (GetExtendedTcpTable, the
// process image name and its command line read from the PEB); elsewhere
// lookup() reports nothing found. classify() and masked() are pure and
// unit-tested through tests/launcher_test.cpp (Qt5Core only).

#include <QString>
#include <QtGlobal>

struct PortOwner {
    bool found = false;
    qint64 pid = 0;
    QString image;        // base name, e.g. "wfweb.exe"; may be empty
    QString commandLine;  // empty when it could not be read

    bool isWfweb = false;      // image is wfweb(.exe)
    bool isLan = false;        // command line carries a bare --lan flag
    bool fromLauncher = false; // command line carries the launcher's -l log path

    // Derives the three flags from image and commandLine.
    void classify(const QString &launcherLogPath);

    // The command line with the --lan-pass value replaced by ****.
    static QString masked(const QString &commandLine);

    static PortOwner lookup(quint16 port, const QString &launcherLogPath);

    // TerminateProcess and wait for it to go; false with a reason otherwise.
    static bool terminate(qint64 pid, QString *error);
};

#endif // PORTOWNER_H
