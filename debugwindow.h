#ifndef DEBUGWINDOW_H
#define DEBUGWINDOW_H

#include <QDialog>
#include <QDebug>
#include <QMap>
#include <QMultiMap>
#include <QQueue>
#include <QTimer>

#include "cachingqueue.h"
#include "wfviewtypes.h"
#include "scrolltest.h"

namespace Ui {
class debugWindow;
}

class debugWindow : public QDialog
{
    Q_OBJECT

private slots:
    void getCache();
    void getQueue();
    void on_cachePause_clicked(bool checked);
    void on_queuePause_clicked(bool checked);
    void on_cacheInterval_textChanged(QString text);
    void on_queueInterval_textChanged(QString text);

public:
    explicit debugWindow(QWidget *parent = nullptr);
    ~debugWindow();

private:
    Ui::debugWindow *ui;
    cachingQueue* queue = Q_NULLPTR;
    QMultiMap <queuePriority,queueItem> queueItems;
    QMap<funcs,cacheItem> cacheItems;
    QString getValue(QVariant val);
    QTimer cacheTimer;
    QTimer queueTimer;
};

#endif // DEBUGWINDOW_H
