#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QComboBox>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>

#include "launchconfig.h"
#include "logparser.h"
#include "logtail.h"
#include "serverprocess.h"
#include "statuspoller.h"

class QAction;
class QCheckBox;
class QCloseEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QSystemTrayIcon;

// A combo box that announces its popup, so the serial port and audio device
// lists are refreshed at the moment the user looks at them.
class RefreshingComboBox : public QComboBox
{
    Q_OBJECT
public:
    using QComboBox::QComboBox;
signals:
    void aboutToShowPopup();
protected:
    void showPopup() override;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Also used by --start / --tray and by the tray menu.
    void startServer();
    void showAndRaise();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildTray();
    void loadConfig();
    void saveConfig();
    void applyConfigToForm();
    void applyFormToConfig();
    void refreshSerialPorts();
    void refreshAudioDevices();
    void selectByData(QComboBox *combo, const QString &data, const QString &missingLabel);
    void setFormEnabled(bool enabled);
    void updateButtons();

    void onStartStop();
    void stopServer();
    void quitAfterStop();
    void openBrowser();
    void copyUrl();
    void browseSettingsFile();
    void openLogFolder();
    bool portInUse(int port) const;
    // The port is taken: say by whom and, for another wfweb, offer to stop
    // it. True when the port is free to use afterwards.
    bool resolvePortConflict(int port);

    void onServerState(ServerProcess::State state);
    void onServerFinished(int exitCode, bool crashed);
    void onServerFailedToStart(const QString &error);
    void onLogLine(const QString &text);
    void onListening(bool https);
    void onStatus(const QJsonObject &info);
    void onProbeTimeout();

    void appendLauncherLine(const QString &text);
    void appendLine(const QString &stamp, const QString &text, LogLine::Level level,
                    const QString &color = QString());
    void setWebStatus(const QString &text, bool ok);
    void setRigStatus(const QString &text, bool ok);
    // From the log, shown until the REST poll reports the rig connected.
    void setRigHint(const QString &text, bool ok);

    void setAutostart(bool enabled);
    bool autostartEnabled() const;

    LaunchConfig m_cfg;
    ServerProcess m_server;
    LogTail m_tail;
    StatusPoller m_poller;

    QString m_exePath;
    QString m_logPath;
    QString m_url;
    QString m_rigHint;
    bool m_rigHintOk = true;
    bool m_rigConnected = false;
    bool m_quitWhenStopped = false;

    // Rig connection
    QGroupBox *m_rigGroup = nullptr;
    QRadioButton *m_usbRadio = nullptr;
    QRadioButton *m_lanRadio = nullptr;
    RefreshingComboBox *m_serialCombo = nullptr;
    QComboBox *m_manufacturerCombo = nullptr;
    QLineEdit *m_civEdit = nullptr;
    RefreshingComboBox *m_audioCombo = nullptr;
    QLineEdit *m_lanHostEdit = nullptr;
    QLineEdit *m_lanUserEdit = nullptr;
    QLineEdit *m_lanPassEdit = nullptr;
    QCheckBox *m_noAutoConnectCheck = nullptr;
    QList<QWidget *> m_usbWidgets;
    QList<QWidget *> m_lanWidgets;

    // Server
    QGroupBox *m_serverGroup = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_settingsEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QLineEdit *m_extraEdit = nullptr;
    QCheckBox *m_startOnOpenCheck = nullptr;
    QCheckBox *m_openBrowserCheck = nullptr;
    QCheckBox *m_autostartCheck = nullptr;

    // Control and status
    QPushButton *m_startStopButton = nullptr;
    QPushButton *m_openBrowserButton = nullptr;
    QPushButton *m_copyUrlButton = nullptr;
    QLabel *m_webStatusLabel = nullptr;
    QLabel *m_rigStatusLabel = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QPushButton *m_logFolderButton = nullptr;

    // Tray
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_trayStartStop = nullptr;
    QAction *m_trayOpenBrowser = nullptr;
};

#endif // MAINWINDOW_H
