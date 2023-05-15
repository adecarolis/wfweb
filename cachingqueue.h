#ifndef CACHINGQUEUE_H
#define CACHINGQUEUE_H
#include <QCoreApplication>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QMap>
#include <QMultiMap>
#include <QVariant>
#include <QQueue>
#include <QRect>
#include <QWaitCondition>
#include <QDateTime>

#include "wfviewtypes.h"
#include "rigidentities.h"

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
    QVariant param=QVariant();
    bool recurring = false;
};

struct cacheItem {
    cacheItem () {};
    cacheItem (funcs command, QVariant value) : command(command), value(value) {};

    funcs command = funcNone;
    QDateTime req=QDateTime();
    QDateTime reply=QDateTime();
    QVariant value=QVariant();
};

class cachingQueue : public QThread
{
    Q_OBJECT

signals:
    void haveCommand(queueItemType type, funcs func, QVariant param);
    void sendValue(cacheItem item);

public slots:
    // Can be called directly or via emit.
    void receiveValue(funcs func, QVariant value);

private:

    static cachingQueue *instance;
    static QMutex instanceMutex;

    QMutex mutex;

    QMultiMap <queuePriority,queueItem> queue;
    QMap<funcs,cacheItem> cache;
    QQueue<cacheItem> items;

    // Command to set cache value
    void setCache(funcs func, QVariant val);
    queuePriority isRecurring(funcs func);


    // Various other values
    bool aborted=false;
    QWaitCondition waiting;
    quint64 queueInterval=0; // Don't start the timer!

    // Functions
    void run();

protected:
    cachingQueue(QObject* parent = Q_NULLPTR) : QThread(parent) {};
    ~cachingQueue();

public:
    cachingQueue(cachingQueue &other) = delete;
    void operator=(const cachingQueue &) = delete;

    static cachingQueue *getInstance(QObject* parent = Q_NULLPTR);
    void message(QString msg);
    void add(queuePriority prio ,funcs func, bool recurring=false);
    void add(queuePriority prio,queueItem item);
    void addUnique(queuePriority prio ,funcs func, bool recurring=false);
    void addUnique(queuePriority prio,queueItem item);
    void del(funcs func);
    void clear();
    void interval(quint64 val);
    void updateCache(bool reply, funcs func, QVariant value=QVariant());

    cacheItem getCache(funcs func);

    QMap<funcs,cacheItem> getCacheItems();
    QMultiMap <queuePriority,queueItem> getQueueItems();
    void unlockMutex();
};

#endif // CACHINGQUEUE_H
