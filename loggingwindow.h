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
#include <QScrollBar>
#include <QProcess>
#include <QFileInfo>
#include <QDir>

#include "logcategories.h"

namespace Ui {
class loggingWindow;
}

class loggingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit loggingWindow(QString logFilename, QWidget *parent = 0);
    ~loggingWindow();
    void acceptLogText(QString text);

public slots:
    void setInitialDebugState(bool debugModeEnabled);

private slots:
    void connectedToHost();
    void disconnectedFromHost();
    void handleDataFromLoggingHost();
    void handleLoggingHostError(QAbstractSocket::SocketError);
    void showEvent(QShowEvent* event);
    void on_clearDisplayBtn_clicked();

    void on_openDirBtn_clicked();

    void on_openLogFileBtn_clicked();

    void on_sendToPasteBtn_clicked();

    void on_annotateBtn_clicked();

    void on_userAnnotationText_returnPressed();

    void on_copyPathBtn_clicked();

    void on_debugBtn_clicked(bool checked);

    void on_toBottomBtn_clicked();

signals:
    void setDebugMode(bool debugOn);

private:
    Ui::loggingWindow* ui;
    QString logFilename;
    QString logDirectory;
    QClipboard *clipboard;
    QMessageBox URLmsgBox;
    QScrollBar *vertLogScroll;
    QScrollBar *horizLogScroll;
    QMutex textMutex;
    QTcpSocket *socket;
    void sendToTermbin();
};

#endif // LOGGINGWINDOW_H
