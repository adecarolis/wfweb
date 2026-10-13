#include "mainwindow.h"

#include "portowner.h"
#include "wfweb_version.h"

#include <QApplication>
#include <QAudioDeviceInfo>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTcpSocket>
#include <QThread>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char kCertHintKey[] = "certHintShown";
const char kAutostartValue[] = "wfweb-launcher";
#ifdef Q_OS_WIN
const char kRunKey[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char kServerExe[] = "wfweb.exe";
#else
const char kServerExe[] = "wfweb";
#endif

const char kColorMuted[] = "#808080";
const char kColorWarning[] = "#b26a00";
const char kColorError[] = "#c62828";
const char kColorLauncher[] = "#1565c0";
const char kColorOk[] = "#2e7d32";

QString escaped(const QString &text)
{
    return text.toHtmlEscaped();
}

QString now()
{
    return QTime::currentTime().toString("hh:mm:ss");
}

} // namespace

void RefreshingComboBox::showPopup()
{
    emit aboutToShowPopup();
    QComboBox::showPopup();
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("wfweb ") + WFWEB_VERSION);
    setWindowIcon(QIcon(":/wfweb.png"));

    m_exePath = QDir(QCoreApplication::applicationDirPath()).filePath(kServerExe);
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_logPath = QDir(dataDir).filePath("wfweb-server.log");

    buildUi();
    buildTray();
    loadConfig();
    applyConfigToForm();
    updateButtons();

    connect(&m_server, &ServerProcess::stateChanged, this, &MainWindow::onServerState);
    connect(&m_server, &ServerProcess::finished, this, &MainWindow::onServerFinished);
    connect(&m_server, &ServerProcess::failedToStart, this, &MainWindow::onServerFailedToStart);
    connect(&m_tail, &LogTail::line, this, &MainWindow::onLogLine);
    connect(&m_poller, &StatusPoller::listening, this, &MainWindow::onListening);
    connect(&m_poller, &StatusPoller::status, this, &MainWindow::onStatus);
    connect(&m_poller, &StatusPoller::timedOut, this, &MainWindow::onProbeTimeout);

    setWebStatus("stopped", true);
    setRigStatus(QString(), true);
    appendLauncherLine(QStringLiteral("Server binary: %1").arg(QDir::toNativeSeparators(m_exePath)));
    if (!QFileInfo::exists(m_exePath))
        appendLine(now(), QStringLiteral("%1 was not found next to the launcher.").arg(kServerExe), LogLine::Warning);
}

MainWindow::~MainWindow()
{
    if (m_rigGroup->isEnabled())
        applyFormToConfig();
    saveConfig();
}

// --- UI --------------------------------------------------------------------

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    root->setSpacing(6);

    // Rig connection. Kept to four rows: on a 200 % display a 1080p screen
    // is only 540 logical pixels tall, and the log pane must still fit.
    m_rigGroup = new QGroupBox("Rig connection", central);
    auto *rig = new QGridLayout(m_rigGroup);
    rig->setContentsMargins(8, 4, 8, 6);
    rig->setVerticalSpacing(4);
    rig->setColumnStretch(2, 3);
    rig->setColumnStretch(4, 1);
    rig->setColumnStretch(6, 1);

    m_usbRadio = new QRadioButton("USB", m_rigGroup);
    m_lanRadio = new QRadioButton("LAN", m_rigGroup);
    m_usbRadio->setChecked(true);

    m_serialCombo = new RefreshingComboBox(m_rigGroup);
    m_serialCombo->setToolTip("Auto looks for an Icom by its USB serial number, as wfweb does on its own.");
    m_manufacturerCombo = new QComboBox(m_rigGroup);
    m_manufacturerCombo->addItem("Icom", int(LaunchConfig::Icom));
    m_manufacturerCombo->addItem("Kenwood", int(LaunchConfig::Kenwood));
    m_manufacturerCombo->addItem("Yaesu", int(LaunchConfig::Yaesu));
    m_civEdit = new QLineEdit(m_rigGroup);
    m_civEdit->setPlaceholderText("auto");
    m_civEdit->setToolTip("CI-V address, hex or decimal (0x94 or 148 for an IC-7300). Leave empty to use the rig file's address.");
    m_civEdit->setMaximumWidth(110);
    m_audioCombo = new RefreshingComboBox(m_rigGroup);
    m_audioCombo->setToolTip("The rig's USB audio input. Default picks a USB Audio CODEC device when there is one.");

    m_lanHostEdit = new QLineEdit(m_rigGroup);
    m_lanHostEdit->setPlaceholderText("192.168.1.50 or hostname");
    m_lanUserEdit = new QLineEdit(m_rigGroup);
    m_lanPassEdit = new QLineEdit(m_rigGroup);
    m_lanPassEdit->setEchoMode(QLineEdit::Password);
    m_noAutoConnectCheck = new QCheckBox("Don't connect to the rig at start (use Reconnect in the web UI)", m_rigGroup);

    auto *serialLabel = new QLabel("Serial port", m_rigGroup);
    auto *manufacturerLabel = new QLabel("Manufacturer", m_rigGroup);
    auto *civLabel = new QLabel("CI-V address", m_rigGroup);
    auto *audioLabel = new QLabel("Audio device", m_rigGroup);
    auto *hostLabel = new QLabel("Address", m_rigGroup);
    auto *userLabel = new QLabel("User name", m_rigGroup);
    auto *passLabel = new QLabel("Password", m_rigGroup);

    int row = 0;
    rig->addWidget(m_usbRadio, row, 0);
    rig->addWidget(serialLabel, row, 1);
    rig->addWidget(m_serialCombo, row, 2);
    rig->addWidget(manufacturerLabel, row, 3);
    rig->addWidget(m_manufacturerCombo, row, 4);
    rig->addWidget(civLabel, row, 5);
    rig->addWidget(m_civEdit, row, 6);
    ++row;
    rig->addWidget(audioLabel, row, 1);
    rig->addWidget(m_audioCombo, row, 2, 1, 5);
    ++row;
    rig->addWidget(m_lanRadio, row, 0);
    rig->addWidget(hostLabel, row, 1);
    rig->addWidget(m_lanHostEdit, row, 2);
    rig->addWidget(userLabel, row, 3);
    rig->addWidget(m_lanUserEdit, row, 4);
    rig->addWidget(passLabel, row, 5);
    rig->addWidget(m_lanPassEdit, row, 6);
    ++row;
    rig->addWidget(m_noAutoConnectCheck, row, 1, 1, 6);

    m_usbWidgets = {serialLabel, m_serialCombo, manufacturerLabel, m_manufacturerCombo,
                    civLabel, m_civEdit, audioLabel, m_audioCombo};
    m_lanWidgets = {hostLabel, m_lanHostEdit, userLabel, m_lanUserEdit,
                    passLabel, m_lanPassEdit, m_noAutoConnectCheck};
    root->addWidget(m_rigGroup);

    // Server
    m_serverGroup = new QGroupBox("Server", central);
    auto *srv = new QGridLayout(m_serverGroup);
    srv->setContentsMargins(8, 4, 8, 6);
    srv->setVerticalSpacing(4);
    srv->setColumnStretch(1, 1);
    srv->setColumnStretch(3, 1);

    m_portSpin = new QSpinBox(m_serverGroup);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(LaunchConfig::defaultWebPort);
    m_portSpin->setToolTip("The browser connects here (HTTPS). The REST API for scripts is on the next port up.");
    m_nameEdit = new QLineEdit(m_serverGroup);
    m_nameEdit->setPlaceholderText("rig model");
    m_nameEdit->setToolTip("Shown in the web UI top bar and browser tab instead of the rig model (-n).");
    m_settingsEdit = new QLineEdit(m_serverGroup);
    m_settingsEdit->setPlaceholderText("default location");
    m_settingsEdit->setToolTip("Optional: wfweb's own settings file (-s). Leave empty to keep using its default location.");
    m_browseButton = new QPushButton("Browse…", m_serverGroup);
    m_extraEdit = new QLineEdit(m_serverGroup);
    m_extraEdit->setPlaceholderText("anything else from wfweb --help, e.g. --rigctld-port 4532");
    m_startOnOpenCheck = new QCheckBox("Start the server when the launcher opens", m_serverGroup);
    m_openBrowserCheck = new QCheckBox("Open the browser once it is up", m_serverGroup);
    m_autostartCheck = new QCheckBox("Start the launcher with Windows, in the tray", m_serverGroup);

    row = 0;
    srv->addWidget(new QLabel("Web port", m_serverGroup), row, 0);
    srv->addWidget(m_portSpin, row, 1, Qt::AlignLeft);
    srv->addWidget(new QLabel("Name tag", m_serverGroup), row, 2);
    srv->addWidget(m_nameEdit, row, 3);
    ++row;
    srv->addWidget(new QLabel("Settings file", m_serverGroup), row, 0);
    srv->addWidget(m_settingsEdit, row, 1, 1, 3);
    srv->addWidget(m_browseButton, row, 4);
    ++row;
    srv->addWidget(new QLabel("Extra arguments", m_serverGroup), row, 0);
    srv->addWidget(m_extraEdit, row, 1, 1, 4);
    ++row;
    auto *checks = new QHBoxLayout;
    checks->addWidget(m_startOnOpenCheck);
    checks->addWidget(m_openBrowserCheck);
    checks->addWidget(m_autostartCheck);
    checks->addStretch(1);
    srv->addLayout(checks, row, 0, 1, 5);
#ifndef Q_OS_WIN
    m_autostartCheck->hide();
#endif
    root->addWidget(m_serverGroup);

    // Controls
    auto *buttons = new QHBoxLayout;
    m_startStopButton = new QPushButton("Start", central);
    m_startStopButton->setMinimumWidth(110);
    m_startStopButton->setDefault(true);
    m_openBrowserButton = new QPushButton("Open browser", central);
    m_copyUrlButton = new QPushButton("Copy URL", central);
    buttons->addWidget(m_startStopButton);
    buttons->addWidget(m_openBrowserButton);
    buttons->addWidget(m_copyUrlButton);
    buttons->addStretch(1);
    root->addLayout(buttons);

    auto *status = new QHBoxLayout;
    m_webStatusLabel = new QLabel(central);
    m_rigStatusLabel = new QLabel(central);
    m_webStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status->addWidget(m_webStatusLabel, 3);
    status->addWidget(m_rigStatusLabel, 2);
    root->addLayout(status);

    // Log
    m_logView = new QPlainTextEdit(central);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_logView->setMinimumHeight(80);
    root->addWidget(m_logView, 1);

    auto *bottom = new QHBoxLayout;
    auto *logLabel = new QLabel(QStringLiteral("Log: %1").arg(QDir::toNativeSeparators(m_logPath)), central);
    logLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_logFolderButton = new QPushButton("Open log folder", central);
    bottom->addWidget(logLabel, 1);
    bottom->addWidget(m_logFolderButton);
    root->addLayout(bottom);

    setCentralWidget(central);
    // Long device names must not dictate the window's minimum width.
    for (QComboBox *combo : {static_cast<QComboBox *>(m_serialCombo), static_cast<QComboBox *>(m_audioCombo)}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(18);
    }

    // Fit the screen: at 150 % scaling a 1080p display is 1280x720 logical,
    // at 200 % only 960x540.
    QRect available(0, 0, 1280, 800);
    if (const QScreen *screen = QGuiApplication::primaryScreen())
        available = screen->availableGeometry();
    QSize target(qMin(760, available.width() - 40), qMin(580, available.height() - 60));
    target = target.expandedTo(minimumSizeHint());
    resize(target);
    // Centred, not where Windows cascades it: on a scaled display the
    // cascade offset alone can push the right edge off the screen.
    move(available.center() - QRect(QPoint(), target).center());

    connect(m_usbRadio, &QRadioButton::toggled, this, [this](bool usb) {
        for (QWidget *w : m_usbWidgets) w->setEnabled(usb);
        for (QWidget *w : m_lanWidgets) w->setEnabled(!usb);
    });
    connect(m_serialCombo, &RefreshingComboBox::aboutToShowPopup, this, &MainWindow::refreshSerialPorts);
    connect(m_audioCombo, &RefreshingComboBox::aboutToShowPopup, this, &MainWindow::refreshAudioDevices);
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::browseSettingsFile);
    connect(m_autostartCheck, &QCheckBox::toggled, this, &MainWindow::setAutostart);
    connect(m_startStopButton, &QPushButton::clicked, this, &MainWindow::onStartStop);
    connect(m_openBrowserButton, &QPushButton::clicked, this, &MainWindow::openBrowser);
    connect(m_copyUrlButton, &QPushButton::clicked, this, &MainWindow::copyUrl);
    connect(m_logFolderButton, &QPushButton::clicked, this, &MainWindow::openLogFolder);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(windowIcon(), this);
    auto *menu = new QMenu(this);
    m_trayStartStop = menu->addAction("Start", this, &MainWindow::onStartStop);
    m_trayOpenBrowser = menu->addAction("Open browser", this, &MainWindow::openBrowser);
    menu->addAction("Show window", this, &MainWindow::showAndRaise);
    menu->addSeparator();
    menu->addAction("Quit", this, &MainWindow::quitAfterStop);
    m_tray->setContextMenu(menu);
    m_tray->setToolTip("wfweb");
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showAndRaise();
    });
    m_tray->show();
}

void MainWindow::showAndRaise()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

// --- Config <-> form -------------------------------------------------------

void MainWindow::loadConfig()
{
    QSettings s;
    m_cfg.load(s);
}

void MainWindow::saveConfig()
{
    QSettings s;
    m_cfg.save(s);
}

void MainWindow::applyConfigToForm()
{
    (m_cfg.transport == LaunchConfig::Lan ? m_lanRadio : m_usbRadio)->setChecked(true);
    refreshSerialPorts();
    selectByData(m_serialCombo, m_cfg.serialPort, "%1 (not present)");
    m_manufacturerCombo->setCurrentIndex(qMax(0, m_manufacturerCombo->findData(int(m_cfg.manufacturer))));
    m_civEdit->setText(m_cfg.civAddress);
    refreshAudioDevices();
    selectByData(m_audioCombo, m_cfg.audioDevice, "%1 (not present)");
    m_lanHostEdit->setText(m_cfg.lanHost);
    m_lanUserEdit->setText(m_cfg.lanUser);
    m_lanPassEdit->setText(m_cfg.lanPassword);
    m_noAutoConnectCheck->setChecked(m_cfg.noAutoConnect);
    m_portSpin->setValue(m_cfg.webPort);
    m_nameEdit->setText(m_cfg.nameTag);
    m_settingsEdit->setText(m_cfg.settingsFile);
    m_extraEdit->setText(m_cfg.extraArguments);
    m_startOnOpenCheck->setChecked(m_cfg.startServerOnOpen);
    m_openBrowserCheck->setChecked(m_cfg.openBrowserOnStart);
    m_autostartCheck->setChecked(autostartEnabled());
    // Apply the USB/LAN enabling for the initial state too.
    emit m_usbRadio->toggled(m_usbRadio->isChecked());
}

void MainWindow::applyFormToConfig()
{
    m_cfg.transport = m_lanRadio->isChecked() ? LaunchConfig::Lan : LaunchConfig::Usb;
    m_cfg.serialPort = m_serialCombo->currentData().toString();
    m_cfg.manufacturer = LaunchConfig::Manufacturer(m_manufacturerCombo->currentData().toInt());
    m_cfg.civAddress = m_civEdit->text().trimmed();
    m_cfg.audioDevice = m_audioCombo->currentData().toString();
    m_cfg.lanHost = m_lanHostEdit->text().trimmed();
    m_cfg.lanUser = m_lanUserEdit->text();
    m_cfg.lanPassword = m_lanPassEdit->text();
    m_cfg.noAutoConnect = m_noAutoConnectCheck->isChecked();
    m_cfg.webPort = m_portSpin->value();
    m_cfg.nameTag = m_nameEdit->text().trimmed();
    const QString settings = m_settingsEdit->text().trimmed();
    // The server resolves -s against its working directory; hand it an
    // absolute path so the file is where the user expects.
    m_cfg.settingsFile = settings.isEmpty()
        ? QString()
        : QDir::toNativeSeparators(QFileInfo(settings).absoluteFilePath());
    m_cfg.extraArguments = m_extraEdit->text();
    m_cfg.startServerOnOpen = m_startOnOpenCheck->isChecked();
    m_cfg.openBrowserOnStart = m_openBrowserCheck->isChecked();
}

void MainWindow::refreshSerialPorts()
{
    const QString current = m_serialCombo->currentData().toString();
    m_serialCombo->clear();
    m_serialCombo->addItem("Auto (first Icom found)", QString());
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        QString label = info.portName();
        if (!info.description().isEmpty())
            label += QStringLiteral(" — ") + info.description();
        if (!info.serialNumber().isEmpty())
            label += QStringLiteral(" (%1)").arg(info.serialNumber());
        m_serialCombo->addItem(label, info.portName());
    }
    selectByData(m_serialCombo, current, "%1 (not present)");
}

void MainWindow::refreshAudioDevices()
{
    const QString current = m_audioCombo->currentData().toString();
    m_audioCombo->clear();
    m_audioCombo->addItem("Default (USB Audio CODEC when present)", QString());
    QStringList seen;
    // The same list the server prints for --audio-device list.
    for (const QAudioDeviceInfo &device : QAudioDeviceInfo::availableDevices(QAudio::AudioInput)) {
        const QString name = device.deviceName();
        if (name.isEmpty() || seen.contains(name))
            continue;
        seen << name;
        m_audioCombo->addItem(name, name);
    }
    selectByData(m_audioCombo, current, "%1 (not present)");
}

void MainWindow::selectByData(QComboBox *combo, const QString &data, const QString &missingLabel)
{
    int index = combo->findData(data);
    if (index < 0 && !data.isEmpty()) {
        // Keep a saved choice visible even when the device is unplugged.
        combo->addItem(missingLabel.arg(data), data);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(qMax(0, index));
}

void MainWindow::setFormEnabled(bool enabled)
{
    m_rigGroup->setEnabled(enabled);
    m_serverGroup->setEnabled(enabled);
}

void MainWindow::updateButtons()
{
    QString label;
    bool enabled = true;
    switch (m_server.state()) {
    case ServerProcess::Stopped:  label = "Start"; break;
    case ServerProcess::Starting:
    case ServerProcess::Running:  label = "Stop"; break;
    case ServerProcess::Stopping: label = "Stopping…"; enabled = false; break;
    }
    m_startStopButton->setText(label);
    m_startStopButton->setEnabled(enabled);
    const bool up = m_server.isRunning() && !m_url.isEmpty();
    m_openBrowserButton->setEnabled(up);
    m_copyUrlButton->setEnabled(up);
    if (m_trayStartStop) {
        m_trayStartStop->setText(label);
        m_trayStartStop->setEnabled(enabled);
    }
    if (m_trayOpenBrowser)
        m_trayOpenBrowser->setEnabled(up);
}

// --- Actions ---------------------------------------------------------------

void MainWindow::onStartStop()
{
    if (m_server.isRunning())
        stopServer();
    else if (m_server.state() == ServerProcess::Stopped)
        startServer();
}

void MainWindow::startServer()
{
    if (m_server.state() != ServerProcess::Stopped)
        return;
    applyFormToConfig();
    saveConfig();

    if (m_cfg.transport == LaunchConfig::Lan && m_cfg.lanHost.isEmpty()) {
        showAndRaise();
        QMessageBox::warning(this, "wfweb", "Enter the radio's LAN address first.");
        return;
    }
    if (!QFileInfo::exists(m_exePath)) {
        showAndRaise();
        QMessageBox::critical(this, "wfweb",
            QStringLiteral("%1 was not found next to the launcher:\n%2")
                .arg(kServerExe, QDir::toNativeSeparators(m_exePath)));
        return;
    }
    if (portInUse(m_cfg.webPort) && !resolvePortConflict(m_cfg.webPort))
        return;

    // A fresh file, so nothing from the previous run shows up in the pane
    // before the server truncates it.
    QDir().mkpath(QFileInfo(m_logPath).absolutePath());
    QFile::remove(m_logPath);

    m_url.clear();
    m_rigHint.clear();
    m_rigConnected = false;
    setRigStatus(QString(), true);

    const QStringList args = m_cfg.toArguments(QDir::toNativeSeparators(m_logPath));
    m_server.start(m_exePath, args);
    appendLauncherLine(QStringLiteral("Starting: %1").arg(m_server.commandLine()));
    m_tail.follow(m_logPath);
    m_poller.begin(m_cfg.restProbeUrls());
}

void MainWindow::stopServer()
{
    if (!m_server.isRunning())
        return;
    appendLauncherLine("Stopping wfweb (q on its stdin)…");
    m_server.stop();
}

void MainWindow::quitAfterStop()
{
    m_quitWhenStopped = true;
    hide();
    if (m_server.isRunning())
        stopServer();
    else if (m_server.state() == ServerProcess::Stopped)
        qApp->quit();
    // Stopping: onServerFinished quits.
}

void MainWindow::openBrowser()
{
    if (m_url.isEmpty())
        return;
    QSettings s;
    if (m_url.startsWith("https") && !s.value(kCertHintKey, false).toBool()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle("wfweb");
        box.setText("Your browser will warn about the certificate.");
        box.setInformativeText(
            "wfweb serves the web UI over HTTPS with a certificate it made on this computer, "
            "so the browser cannot verify it. The warning is expected: choose Advanced, then "
            "Proceed (or Accept the risk). The browser remembers the choice.");
        auto *dontShow = new QCheckBox("Don't show this again", &box);
        box.setCheckBox(dontShow);
        box.exec();
        if (dontShow->isChecked())
            s.setValue(kCertHintKey, true);
    }
    QDesktopServices::openUrl(QUrl(m_url));
}

void MainWindow::copyUrl()
{
    if (!m_url.isEmpty())
        QApplication::clipboard()->setText(m_url);
}

void MainWindow::browseSettingsFile()
{
    const QString current = m_settingsEdit->text().trimmed();
    const QString startDir = current.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : QFileInfo(current).absolutePath();
    const QString file = QFileDialog::getSaveFileName(this, "wfweb settings file", startDir,
                                                      "Settings (*.ini);;All files (*)", nullptr,
                                                      QFileDialog::DontConfirmOverwrite);
    if (!file.isEmpty())
        m_settingsEdit->setText(QDir::toNativeSeparators(file));
}

void MainWindow::openLogFolder()
{
    const QString dir = QFileInfo(m_logPath).absolutePath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

bool MainWindow::portInUse(int port) const
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, quint16(port));
    const bool open = socket.waitForConnected(300);
    socket.abort();
    return open;
}

bool MainWindow::resolvePortConflict(int port)
{
    const PortOwner owner = PortOwner::lookup(quint16(port), m_logPath);

    QString who;
    if (owner.found)
        who = owner.image.isEmpty() ? QStringLiteral("PID %1").arg(owner.pid)
                                    : QStringLiteral("%1 (PID %2)").arg(owner.image).arg(owner.pid);
    setWebStatus(who.isEmpty() ? QStringLiteral("port %1 is already in use").arg(port)
                               : QStringLiteral("port %1 is in use by %2").arg(port).arg(who), false);
    appendLauncherLine(who.isEmpty()
        ? QStringLiteral("Port %1 is already in use, not starting.").arg(port)
        : QStringLiteral("Port %1 is in use by %2, not starting.").arg(port).arg(who));
    if (!owner.commandLine.isEmpty())
        appendLauncherLine(QStringLiteral("Its command line: %1").arg(PortOwner::masked(owner.commandLine)));
    showAndRaise();

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("wfweb");
    box.setText(QStringLiteral("Port %1 is already in use.").arg(port));

    if (!owner.isWfweb) {
        box.setInformativeText(owner.found
            ? QStringLiteral("It is held by %1, which is not wfweb. Stop that program, or choose a different web port.").arg(who)
            : QStringLiteral("Is another wfweb running? Stop it, or choose a different web port."));
        box.addButton(QMessageBox::Ok);
        box.exec();
        return false;
    }

    QString info = QStringLiteral("Another wfweb is running as PID %1").arg(owner.pid);
    if (!owner.commandLine.isEmpty()) {
        // One flag per line: a message box grows to fit its longest line,
        // and a full command line would push the buttons off the screen.
        QString shown = PortOwner::masked(owner.commandLine);
        shown.replace(QRegularExpression("\\s+(?=-)"), QStringLiteral("\n    "));
        // A zero-width space after each separator lets the exe path wrap
        // instead of dictating the dialog's width.
        shown.replace(QRegularExpression("([\\\\/])"), QStringLiteral("\\1") + QChar(0x200B));
        info += QStringLiteral(":\n%1").arg(shown);
    }
    info += "\n\n";
    if (owner.fromLauncher)
        info += "It was started by an earlier launcher and can be stopped from here.";
    else
        info += "If it has a console window, press q there for a clean shutdown. "
                "Stopping it from here ends it at once.";
    if (owner.isLan)
        info += "\n\nIt is connected to a LAN rig: stopping it here skips the logout, "
                "and the radio may refuse new connections for a few minutes.";
    box.setInformativeText(info);
    QPushButton *stopButton = box.addButton("Stop it and start", QMessageBox::AcceptRole);
    QPushButton *cancelButton = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancelButton);
    box.exec();
    if (box.clickedButton() != stopButton)
        return false;

    QString error;
    if (!PortOwner::terminate(owner.pid, &error)) {
        appendLine(now(), QStringLiteral("Could not stop PID %1: %2").arg(owner.pid).arg(error), LogLine::Critical);
        QMessageBox::critical(this, "wfweb", QStringLiteral("Could not stop wfweb (PID %1):\n%2").arg(owner.pid).arg(error));
        return false;
    }
    appendLauncherLine(QStringLiteral("Stopped wfweb (PID %1), which held port %2.").arg(owner.pid).arg(port));

    // The listening socket closes a moment after the process; allow a few seconds.
    QElapsedTimer waited;
    waited.start();
    while (portInUse(port) && waited.elapsed() < 5000)
        QThread::msleep(100);
    if (portInUse(port)) {
        setWebStatus(QStringLiteral("port %1 is still in use").arg(port), false);
        appendLauncherLine(QStringLiteral("Port %1 is still in use after stopping PID %2.").arg(port).arg(owner.pid));
        return false;
    }
    return true;
}

// --- Server events ---------------------------------------------------------

void MainWindow::onServerState(ServerProcess::State state)
{
    switch (state) {
    case ServerProcess::Starting:
        setFormEnabled(false);
        setWebStatus(QStringLiteral("starting on port %1…").arg(m_cfg.webPort), true);
        break;
    case ServerProcess::Stopping:
        setWebStatus("stopping…", true);
        break;
    case ServerProcess::Running:
    case ServerProcess::Stopped:
        break;
    }
    updateButtons();
}

void MainWindow::onServerFinished(int exitCode, bool crashed)
{
    m_tail.stop();
    m_poller.stop();
    appendLauncherLine(crashed ? QStringLiteral("wfweb crashed.")
                               : QStringLiteral("wfweb exited with code %1.").arg(exitCode));
    m_url.clear();
    m_rigConnected = false;
    setFormEnabled(true);
    setWebStatus("stopped", true);
    setRigStatus(QString(), true);
    updateButtons();
    if (m_tray)
        m_tray->setToolTip("wfweb");
    if (m_quitWhenStopped)
        qApp->quit();
}

void MainWindow::onServerFailedToStart(const QString &error)
{
    m_tail.stop();
    m_poller.stop();
    appendLine(now(), QStringLiteral("Could not start %1: %2").arg(kServerExe, error), LogLine::Critical);
    setFormEnabled(true);
    setWebStatus("failed to start", false);
    updateButtons();
    if (m_quitWhenStopped) {
        qApp->quit();
        return;
    }
    showAndRaise();
    QMessageBox::critical(this, "wfweb", QStringLiteral("Could not start %1:\n%2").arg(kServerExe, error));
}

void MainWindow::onLogLine(const QString &text)
{
    LogLine line;
    if (!LogParser::parse(text, line)) {
        appendLine(QString(), text, LogLine::Info);
        return;
    }
    appendLine(line.timestamp.toString("hh:mm:ss"), LogParser::tidy(line.message), line.level);

    const LogEvent e = LogParser::classify(line.message);
    switch (e.kind) {
    case LogEvent::WebListening:
        if (!m_poller.isListening())
            setWebStatus(QStringLiteral("port %1 open, waiting for the API…").arg(e.detail), true);
        break;
    case LogEvent::WebFailed:
        setWebStatus(QStringLiteral("failed: port %1 could not be opened").arg(e.detail), false);
        m_poller.stop();
        appendLauncherLine("Stop the server, then pick another web port or stop whatever holds this one.");
        updateButtons();
        break;
    case LogEvent::WebPlainHttp:
        appendLauncherLine("TLS is unavailable, so the web UI is plain HTTP. The microphone only works when the page is opened as localhost.");
        break;
    case LogEvent::SerialOpened:
        setRigHint(QStringLiteral("%1 open, identifying the rig").arg(e.detail), true);
        break;
    case LogEvent::SerialFailed:
        setRigHint(QStringLiteral("%1 could not be opened").arg(e.detail), false);
        break;
    case LogEvent::SerialAutoDetected:
        setRigHint(QStringLiteral("found %1").arg(e.detail), true);
        break;
    case LogEvent::SerialFallback:
        setRigHint("no Icom port found, trying the OS default port", false);
        break;
    case LogEvent::RigModel:
        setRigHint(e.detail, true);
        break;
    case LogEvent::LanLoginFailed:
        setRigHint("LAN login failed: check the user name and password", false);
        break;
    case LogEvent::LanFailed:
        setRigHint(QStringLiteral("LAN: %1").arg(e.detail), false);
        break;
    case LogEvent::Plain:
    case LogEvent::AudioWarning:
    case LogEvent::Note:
        break;
    }
}

void MainWindow::onListening(bool https)
{
    m_url = m_cfg.webUrl(https);
    setWebStatus(QStringLiteral("listening at %1").arg(m_url), true);

    QStringList lanUrls;
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            lanUrls << QStringLiteral("%1://%2:%3/").arg(https ? "https" : "http", address.toString()).arg(m_cfg.webPort);
    }
    m_copyUrlButton->setToolTip(lanUrls.isEmpty() ? QString()
                                                  : "From another device on the LAN:\n" + lanUrls.join('\n'));
    if (m_tray)
        m_tray->setToolTip(QStringLiteral("wfweb — %1").arg(m_url));
    updateButtons();
    if (m_cfg.openBrowserOnStart)
        openBrowser();
}

void MainWindow::onStatus(const QJsonObject &info)
{
    const bool connected = info.value(QStringLiteral("connected")).toBool();
    const QString model = info.value(QStringLiteral("model")).toString();
    if (connected) {
        m_rigConnected = true;
        setRigStatus(model.isEmpty() ? QStringLiteral("connected") : model + " connected", true);
        return;
    }
    m_rigConnected = false;
    if (m_rigHint.isEmpty())
        setRigStatus("waiting for the rig", true);
}

void MainWindow::onProbeTimeout()
{
    setWebStatus(QStringLiteral("no answer on port %1 after %2 s, see the log")
                     .arg(m_cfg.webPort).arg(StatusPoller::deadlineMs / 1000), false);
}

// --- Status and log pane ---------------------------------------------------

void MainWindow::appendLauncherLine(const QString &text)
{
    appendLine(now(), text, LogLine::Info, kColorLauncher);
}

void MainWindow::appendLine(const QString &stamp, const QString &text, LogLine::Level level, const QString &color)
{
    QString tint = color;
    QString tag;
    if (tint.isEmpty()) {
        switch (level) {
        case LogLine::Warning:  tint = kColorWarning; tag = "WRN "; break;
        case LogLine::Critical:
        case LogLine::Fatal:    tint = kColorError; tag = "CRT "; break;
        case LogLine::Debug:    tint = kColorMuted; break;
        case LogLine::Info:     break;
        }
    }
    QString html;
    if (!stamp.isEmpty())
        html += QStringLiteral("<span style=\"color:%1\">%2</span> ").arg(kColorMuted, escaped(stamp));
    const QString body = escaped(tag + text);
    html += tint.isEmpty() ? body : QStringLiteral("<span style=\"color:%1\">%2</span>").arg(tint, body);

    // Keep following the tail unless the user scrolled up. The block cap
    // drops old lines from the top, which defeats the view's own heuristic.
    QScrollBar *bar = m_logView->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 2;
    m_logView->appendHtml(html);
    if (atBottom)
        bar->setValue(bar->maximum());
}

void MainWindow::setWebStatus(const QString &text, bool ok)
{
    m_webStatusLabel->setText(QStringLiteral("<b>Web:</b> <span style=\"color:%1\">%2</span>")
                                  .arg(ok ? kColorOk : kColorError, escaped(text)));
}

void MainWindow::setRigStatus(const QString &text, bool ok)
{
    if (text.isEmpty()) {
        m_rigStatusLabel->clear();
        return;
    }
    m_rigStatusLabel->setText(QStringLiteral("<b>Rig:</b> <span style=\"color:%1\">%2</span>")
                                  .arg(ok ? kColorOk : kColorError, escaped(text)));
}

void MainWindow::setRigHint(const QString &text, bool ok)
{
    m_rigHint = text;
    m_rigHintOk = ok;
    if (!m_rigConnected)
        setRigStatus(text, ok);
}

// --- Autostart (Windows Run key) ------------------------------------------

bool MainWindow::autostartEnabled() const
{
#ifdef Q_OS_WIN
    QSettings run(kRunKey, QSettings::NativeFormat);
    return run.contains(kAutostartValue);
#else
    return false;
#endif
}

void MainWindow::setAutostart(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (enabled)
        run.setValue(kAutostartValue, QStringLiteral("\"%1\" --tray")
                         .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else
        run.remove(kAutostartValue);
#else
    Q_UNUSED(enabled);
#endif
}

// --- Closing ---------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    // A close request that arrives while one of our dialogs is modal (UI
    // Automation can deliver one) must not quit() out from under the
    // dialog's event loop: the window would stay blocked behind a modality
    // that no longer has a dialog.
    if (QApplication::activeModalWidget()) {
        event->ignore();
        return;
    }
    if (m_server.state() == ServerProcess::Stopped) {
        applyFormToConfig();
        saveConfig();
        event->accept();
        qApp->quit();
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle("wfweb");
    box.setText("wfweb is running.");
    box.setInformativeText("Stop the server and quit, or keep it running from the tray?");
    QPushButton *stopButton = box.addButton("Stop and quit", QMessageBox::AcceptRole);
    QPushButton *trayButton = m_tray ? box.addButton("Keep running in tray", QMessageBox::ActionRole) : nullptr;
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(trayButton ? trayButton : stopButton);
    box.exec();

    event->ignore();
    if (box.clickedButton() == stopButton)
        quitAfterStop();
    else if (trayButton && box.clickedButton() == trayButton)
        hide();
}
