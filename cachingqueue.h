#ifndef CACHINGQUEUE_H
#define CACHINGQUEUE_H
#include <QCoreApplication>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QMultiMap>
#include <QVariant>
#include <QRect>
#include <atomic>
#include <QWaitCondition>

#include "wfviewtypes.h"


enum queuePriority {
    // Use prime numbers for priority, so that each queue is processed
    priorityNone=0, priorityImmediate=1, priorityHighest=2, priorityHigh=3, priorityMediumHigh=5, priorityMedium=7, priorityMediumLow=11, priorityLow=19, priorityLowest=23
};

enum queueItemType {
    queueCommandNone,queueCommandGet, queueCommandSet, queueMessage
};

// Command with no param is a get by default
struct queueItem {
    queueItem () {};
    queueItem (funcs command, QVariant param, bool recurring) : type(queueCommandGet), command(command), param(param), recurring(recurring) {};
    queueItem (funcs command, bool recurring) : type(queueCommandGet), command(command), recurring(recurring) {};
    queueItem (funcs command) : type(queueCommandGet), command(command) {};
    queueItem (funcs command, QVariant param) : type(queueCommandSet), command(command), param(param) {};
    queueItem (queueItemType type, funcs command, QVariant param) : type(type), command(command), param(param) {};

    queueItemType type = queueCommandNone;
    funcs command = funcNone;
    QVariant param;
    bool recurring = false;
};

class cachingQueue : public QThread
{
    Q_OBJECT

signals:
    void haveCommand(queueItemType type, funcs func, QVariant param);

private:
    static cachingQueue *instance;
    static QMutex mutex;
    QMultiMap <queuePriority,queueItem> queue;
    QHash<funcs,QVariant> cache;

    // Command to set cache value
    void setCache(funcs func, QVariant val);
    // Various commands to get cache value
    QVariant getCache(funcs func);
    bool getCache(funcs func, quint8& val);
    bool getCache(funcs func, quint16& val);
    bool getCache(funcs func, quint32& val);
    std::atomic<bool> aborted=false;
    QWaitCondition waiting;
    void run();
    quint64 queueInterval=0; // Don't start the timer!

protected:
    cachingQueue(QObject* parent = Q_NULLPTR) : QThread(parent) {};
    ~cachingQueue();

public:
    cachingQueue(cachingQueue &other) = delete;
    void operator=(const cachingQueue &) = delete;

    static cachingQueue *getInstance(QObject* parent = Q_NULLPTR);
    void message(QString msg);
    void add(queuePriority prio ,funcs func, bool recurring=false);
    void add(queuePriority,queueItem);
    void addUnique(queuePriority,queueItem);
    void del(funcs func);
    void clear();
    void interval(quint64 val);
};

#endif // CACHINGQUEUE_H
