#include "launchconfig.h"

#include <QProcess>
#include <QSettings>

QStringList LaunchConfig::toArguments(const QString &logFile) const
{
    QStringList args;
    // A pipe on stdin already disables the status page; the flag makes the
    // behaviour independent of how the handles were inherited.
    args << "--no-tui" << "-l" << logFile;

    if (transport == Lan) {
        args << "--lan" << lanHost.trimmed();
        if (!lanUser.isEmpty()) args << "--lan-user" << lanUser;
        if (!lanPassword.isEmpty()) args << "--lan-pass" << lanPassword;
        if (noAutoConnect) args << "--no-autoconnect";
    } else {
        // Nothing for the serial port when empty: the server's own "auto"
        // looks for an Icom by USB serial number.
        if (!serialPort.isEmpty()) args << "--serial-port" << serialPort;
        if (manufacturer != Icom) args << "--manufacturer" << QString::number(int(manufacturer));
        if (!civAddress.trimmed().isEmpty()) args << "--civ" << civAddress.trimmed();
        if (!audioDevice.isEmpty()) args << "--audio-device" << audioDevice;
    }

    if (webPort > 0 && webPort != defaultWebPort) args << "-p" << QString::number(webPort);
    if (!nameTag.trimmed().isEmpty()) args << "-n" << nameTag.trimmed();
    if (!settingsFile.isEmpty()) args << "-s" << settingsFile;

    args << QProcess::splitCommand(extraArguments);
    return args;
}

QStringList LaunchConfig::restProbeUrls() const
{
    return QStringList{
        QString("http://127.0.0.1:%1/api/v1/radio/info").arg(webPort + 1),
        QString("http://127.0.0.1:%1/api/v1/radio/info").arg(webPort),
    };
}

QString LaunchConfig::webUrl(bool https) const
{
    return QString("%1://localhost:%2/").arg(https ? "https" : "http").arg(webPort);
}

void LaunchConfig::load(const QSettings &s)
{
    transport = s.value("transport", int(Usb)).toInt() == int(Lan) ? Lan : Usb;
    serialPort = s.value("serialPort").toString();
    int man = s.value("manufacturer", int(Icom)).toInt();
    manufacturer = (man == Kenwood) ? Kenwood : (man == Yaesu) ? Yaesu : Icom;
    civAddress = s.value("civAddress").toString();
    audioDevice = s.value("audioDevice").toString();
    lanHost = s.value("lanHost").toString();
    lanUser = s.value("lanUser").toString();
    lanPassword = s.value("lanPassword").toString();
    noAutoConnect = s.value("noAutoConnect", false).toBool();
    webPort = s.value("webPort", defaultWebPort).toInt();
    if (webPort <= 0 || webPort > 65535) webPort = defaultWebPort;
    nameTag = s.value("nameTag").toString();
    settingsFile = s.value("settingsFile").toString();
    extraArguments = s.value("extraArguments").toString();
    startServerOnOpen = s.value("startServerOnOpen", false).toBool();
    openBrowserOnStart = s.value("openBrowserOnStart", true).toBool();
}

void LaunchConfig::save(QSettings &s) const
{
    s.setValue("transport", int(transport));
    s.setValue("serialPort", serialPort);
    s.setValue("manufacturer", int(manufacturer));
    s.setValue("civAddress", civAddress);
    s.setValue("audioDevice", audioDevice);
    s.setValue("lanHost", lanHost);
    s.setValue("lanUser", lanUser);
    s.setValue("lanPassword", lanPassword);
    s.setValue("noAutoConnect", noAutoConnect);
    s.setValue("webPort", webPort);
    s.setValue("nameTag", nameTag);
    s.setValue("settingsFile", settingsFile);
    s.setValue("extraArguments", extraArguments);
    s.setValue("startServerOnOpen", startServerOnOpen);
    s.setValue("openBrowserOnStart", openBrowserOnStart);
}
