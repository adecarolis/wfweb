#include "loggingwindow.h"
#include "ui_loggingwindow.h"

loggingWindow::loggingWindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::loggingWindow)
{
    ui->setupUi(this);
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
    connect(socket, SIGNAL(hostFound()), this, SLOT(handleLoggingHostError()));
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
    qInfo(logGui()) << "Sending data to termbin.com. Standby.";
    socket->connectToHost("termbin.com", 9999);
    ui->sendToPasteBtn->setDisabled(true);
}

void loggingWindow::handleDataFromLoggingHost()
{
    qInfo(logGui()) << "Receiving data from logging host.";
    QString URL;
    QByteArray data = socket->readAll();
    if(data.length() < 256)
    {
        URL = QString(data).trimmed();
        if(!URL.isEmpty())
        {
            clipboard->setText(URL);
            qInfo(logGui()) << "Sent log to URL: " << URL;
            msgBox.setText("Your log has been posted, and the URL has been copied to the clipboard.");
            msgBox.setInformativeText(URL);
            msgBox.exec();
        }
    } else {
        qDebug(logGui()) << "Error, return from logging host too large. Received " << data.length() << " bytes.";
    }
}

void loggingWindow::disconnectedFromHost()
{
    qInfo(logGui()) << "Disconnected from logging host";
    ui->sendToPasteBtn->setDisabled(false);
}

void loggingWindow::connectedToHost()
{
    qInfo(logGui()) << "Connected to logging host";
    QMutexLocker lock(&textMutex);
    QTextStream outText(socket);
    outText << ui->logTextDisplay->toPlainText();
    outText << "\n----------\nSent from wfview version ";
    outText << WFVIEW_VERSION << "\n----------\n";
}

void loggingWindow::handleLoggingHostError(QAbstractSocket::SocketError error)
{
    qInfo(logGui()) << "Error connecting to logging host. Check internet connection. Error code: " << error;
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
#ifdef Q_OS_MAC
    cmd = "open " + logDirectory;
#endif
#ifdef Q_OS_LINUX
    cmd = "xdg-open " + logDirectory;
#endif
#ifdef Q_OS_WIN
    cmd = "start " + logDirectory;
#endif
    system(cmd.toLocal8Bit().data());
}

void loggingWindow::on_openLogFileBtn_clicked()
{
    QString cmd;
#ifdef Q_OS_MAC
    cmd = "open " + logFilename;
#endif
#ifdef Q_OS_LINUX
    cmd = "xdg-open " + logFilename;
#endif
#ifdef Q_OS_WIN
    cmd = "start " + logFilename;
#endif
    system(cmd.toLocal8Bit().data());
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
