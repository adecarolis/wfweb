#include "loggingwindow.h"
#include "ui_loggingwindow.h"

loggingWindow::loggingWindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::loggingWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("Log");
    ui->logTextDisplay->setReadOnly(true);
    ui->userAnnotationText->setFocus();
    ui->annotateBtn->setDefault(true);
    ui->logTextDisplay->setFocusPolicy(Qt::NoFocus);
    ui->annotateBtn->setFocusPolicy(Qt::NoFocus);

#ifdef Q_OS_MAC
    logFilename = QStandardPaths::standardLocations(QStandardPaths::DownloadLocation)[0] + "/wfview.log";
    logDirectory = QStandardPaths::standardLocations(QStandardPaths::DownloadLocation)[0];

#else
    logFilename= QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0] + "/wfview.log";
    logDirectory = QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0];
#endif
    clipboard = QApplication::clipboard();
    socket = new QTcpSocket(this);
    connect(socket, SIGNAL(connected()), this, SLOT(connectedToHost()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(disconnectedFromHost()));
    connect(socket, SIGNAL(readyRead()), this, SLOT(handleDataFromLoggingHost()));
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(handleLoggingHostError(QAbstractSocket::SocketError)));
}

loggingWindow::~loggingWindow()
{
    QMutexLocker lock(&textMutex);
    delete ui;
}

void loggingWindow::acceptLogText(QString text)
{
    QMutexLocker lock(&textMutex);
    ui->logTextDisplay->appendPlainText(text);
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
            msgBox.setText("Your log has been posted, and the URL has been copied to the clipboard.");
            msgBox.setInformativeText("<b>" + URL + "</b>");
            msgBox.exec();
            // For whatever reason, showing the message box hides this window.
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
}

void loggingWindow::handleLoggingHostError(QAbstractSocket::SocketError error)
{
    switch(error)
    {
    case QAbstractSocket::RemoteHostClosedError:
        qInfo(logLogger()) << "Disconnected from logging host.";
        break;

    default:
        qInfo(logLogger()) << "Error connecting to logging host. Check internet connection. Error code: " << error;
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
    int rtnval = 0;
#ifdef Q_OS_MAC
    cmd = "open " + logDirectory;
#endif
#ifdef Q_OS_LINUX
    cmd = "xdg-open " + logDirectory;
#endif
#ifdef Q_OS_WIN
    cmd = "start " + logDirectory;
#endif
    rtnval = system(cmd.toLocal8Bit().data());
    if(rtnval)
        qInfo(logLogger()) << "Error, open log directory command returned error code " << rtnval;
}

void loggingWindow::on_openLogFileBtn_clicked()
{
    QString cmd;
    int rtnval = 0;
#ifdef Q_OS_MAC
    cmd = "open " + logFilename;
#endif
#ifdef Q_OS_LINUX
    cmd = "xdg-open " + logFilename;
#endif
#ifdef Q_OS_WIN
    cmd = "start " + logFilename;
#endif
    rtnval = system(cmd.toLocal8Bit().data());
    if(rtnval)
        qInfo(logLogger()) << "Error, open log file command returned error code " << rtnval;
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
