#ifndef LOGGINGWINDOW_H
#define LOGGINGWINDOW_H

#include <QWidget>
#include <QMutexLocker>
#include <QMutex>
#include <QStandardPaths>
#include <QClipboard>
#include <QTcpSocket>
#include <QTextStream>
#include <QMessageBox>

#include "logcategories.h"

namespace Ui {
class loggingWindow;
}

class loggingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit loggingWindow(QWidget *parent = nullptr);
    ~loggingWindow();
    void acceptLogText(QString text);

private slots:
    void connectedToHost();
    void disconnectedFromHost();
    void handleDataFromLoggingHost();
    void handleLoggingHostError(QAbstractSocket::SocketError);

    void on_clearDisplayBtn_clicked();

    void on_openDirBtn_clicked();

    void on_openLogFileBtn_clicked();

    void on_sendToPasteBtn_clicked();

    void on_annotateBtn_clicked();

    void on_userAnnotationText_returnPressed();

    void on_copyPathBtn_clicked();

private:
    Ui::loggingWindow *ui;
    QClipboard *clipboard;
    QMessageBox msgBox;
    QMutex textMutex;
    QString logFilename;
    QString logDirectory;
    QTcpSocket *socket;
    void sendToTermbin();
};

#endif // LOGGINGWINDOW_H
