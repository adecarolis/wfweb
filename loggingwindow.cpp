#include "loggingwindow.h"
#include "ui_loggingwindow.h"

loggingWindow::loggingWindow(QString logFilename, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::loggingWindow),
    logFilename(logFilename)
{
    ui->setupUi(this);
    this->setWindowTitle("Log");
    ui->logTextDisplay->setReadOnly(true);
    ui->userAnnotationText->setFocus();
    ui->annotateBtn->setDefault(true);
    ui->logTextDisplay->setFocusPolicy(Qt::NoFocus);
    ui->annotateBtn->setFocusPolicy(Qt::NoFocus);
    
    QDir d = QFileInfo(logFilename).absoluteDir();
    logDirectory = d.absolutePath();

    QFont font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
    ui->logTextDisplay->setFont(font);
    ui->userAnnotationText->setFont(font);

    clipboard = QApplication::clipboard();
    socket = new QTcpSocket(this);
    connect(socket, SIGNAL(connected()), this, SLOT(connectedToHost()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(disconnectedFromHost()));
    connect(socket, SIGNAL(readyRead()), this, SLOT(handleDataFromLoggingHost()));
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
    connect(socket, SIGNAL(errorOccurred(QAbstractSocket::SocketError)), this, SLOT(handleLoggingHostError(QAbstractSocket::SocketError)));
#else
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(handleLoggingHostError(QAbstractSocket::SocketError)));
#endif
    vertLogScroll = ui->logTextDisplay->verticalScrollBar();
    horizLogScroll = ui->logTextDisplay->horizontalScrollBar();

    vertLogScroll->setValue(vertLogScroll->maximum());
    horizLogScroll->setValue(horizLogScroll->minimum());
}

loggingWindow::~loggingWindow()
{
    QMutexLocker lock(&textMutex);
    delete ui;
}

void loggingWindow::showEvent(QShowEvent *event)
{
    (void)event;
    on_toBottomBtn_clicked();
}

void loggingWindow::setInitialDebugState(bool debugModeEnabled)
{
    ui->debugBtn->blockSignals(true);
    ui->debugBtn->setChecked(debugModeEnabled);
    ui->debugBtn->blockSignals(false);
}

void loggingWindow::acceptLogText(QString text)
{
    QMutexLocker lock(&textMutex);
    ui->logTextDisplay->appendPlainText(text);
    if(vertLogScroll->value() == vertLogScroll->maximum())
    {
        horizLogScroll->setValue(horizLogScroll->minimum());
    }
}

void loggingWindow::sendToTermbin()
{
    qInfo(logLogger()) << "Sending data to termbin.com. Standby.";
    socket->connectToHost("termbin.com", 9999);
    ui->sendToPasteBtn->setDisabled(true);
}

void loggingWindow::handleDataFromLoggingHost()
{
    qInfo(logLogger()) << "Receiving data from logging host.";
    QString URL;
    QByteArray data = socket->readAll();
    if(data.length() < 256)
    {
        URL = QString(data).trimmed();
        if(!URL.isEmpty())
        {
            clipboard->setText(URL);
            qInfo(logLogger()) << "Sent log to URL: " << URL;
            qInfo(logLogger()) << "This address already copied to the clipboard. Please paste this URL in to your support questions.";
            URLmsgBox.setText("Your log has been posted, and the URL has been copied to the clipboard.");
            URLmsgBox.setInformativeText("<b>" + URL + "</b>");
            URLmsgBox.exec();
            // For whatever reason, showing the message box hides https://termbin.com/ypxbthis window.
            this->show();
            this->raise();
            this->activateWindow();
        }
    } else {
        qDebug(logLogger()) << "Error, return from logging host too large. Received " << data.length() << " bytes.";
    }
}

void loggingWindow::disconnectedFromHost()
{
    qInfo(logLogger()) << "Disconnected from logging host";
    ui->sendToPasteBtn->setDisabled(false);
}

void loggingWindow::connectedToHost()
{
    qInfo(logLogger()) << "Connected to logging host";
    QMutexLocker lock(&textMutex);
    QTextStream outText(socket);
    outText << ui->logTextDisplay->toPlainText();
    outText << "\n----------\nSent from wfview version ";
    outText << WFVIEW_VERSION << "\n----------\n";
    outText.flush();
}

void loggingWindow::handleLoggingHostError(QAbstractSocket::SocketError error)
{
    switch(error)
    {
    case QAbstractSocket::RemoteHostClosedError:
        //qInfo(logLogger()) << "Disconnected from logging host.";
        break;

    default:
        qWarning(logLogger()) << "Error connecting to logging host. Check internet connection. Error code: " << error;
        ui->sendToPasteBtn->setDisabled(false);
        break;
    }
}

void loggingWindow::on_clearDisplayBtn_clicked()
{
    QMutexLocker lock(&textMutex);
    // Only clears the displayed text, not the log file.
    ui->logTextDisplay->clear();
}

void loggingWindow::on_openDirBtn_clicked()
{
    QString cmd;
    bool rtn = false;
    QStringList arg;
    const QFileInfo dir(logDirectory);

#ifdef Q_OS_LINUX
    cmd = "xdg-open";
#elif defined(Q_OS_WIN)
    cmd = QStandardPaths::findExecutable("explorer.exe");
    if (!dir.isDir())
        arg += QLatin1String("/select,");
#else
    cmd = "open";
#endif
    arg += QDir::toNativeSeparators(dir.canonicalFilePath());;
    rtn = QProcess::startDetached(cmd, arg);
    if(!rtn)
        qInfo(logLogger()) << "Error, open log directory" << logDirectory << "command failed";
}

void loggingWindow::on_openLogFileBtn_clicked()
{
    QString cmd;
    bool rtn = false;
#ifdef Q_OS_LINUX
    cmd = "xdg-open";
#elif defined(Q_OS_WIN)
    cmd = QStandardPaths::findExecutable("notepad.exe");
#else
    cmd = "open";
#endif
    rtn = QProcess::startDetached(cmd, { logFilename });
    if(!rtn)
        qInfo(logLogger()) << "Error, open log file command failed";
}

void loggingWindow::on_sendToPasteBtn_clicked()
{
    sendToTermbin();
}

void loggingWindow::on_annotateBtn_clicked()
{
    QMutexLocker lock(&textMutex);
    if(ui->userAnnotationText->text().isEmpty())
        return;
    qInfo(logUser()) << ui->userAnnotationText->text();
    ui->userAnnotationText->clear();
}

void loggingWindow::on_userAnnotationText_returnPressed()
{
    on_annotateBtn_clicked();
}

void loggingWindow::on_copyPathBtn_clicked()
{
    clipboard->setText(logFilename);
}

void loggingWindow::on_debugBtn_clicked(bool checked)
{
    emit setDebugMode(checked);
}

void loggingWindow::on_toBottomBtn_clicked()
{
    vertLogScroll->setValue(vertLogScroll->maximum());
    horizLogScroll->setValue(horizLogScroll->minimum());
}
