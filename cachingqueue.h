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
#include <QRandomGenerator>

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
    queueItem () {}
    queueItem (funcs command, QVariant param, bool recurring, bool sub) : command(command), param(param), sub(sub), recurring(recurring){};
    queueItem (funcs command, QVariant param, bool recurring) : command(command), param(param), sub(false), recurring(recurring){};
    queueItem (funcs command, QVariant param) : command(command), param(param), sub(false), recurring(false){};
    queueItem (funcs command, bool recurring, bool sub=false) : command(command), param(QVariant()), sub(sub), recurring(recurring) {};
    queueItem (funcs command, bool recurring) : command(command), param(QVariant()), sub(false), recurring(recurring) {};
    queueItem (funcs command) : command(command), param(QVariant()), sub(false), recurring(false){};
    funcs command;
    QVariant param;
    bool sub;
    bool recurring;
};

struct cacheItem {
    cacheItem () {};
    cacheItem (funcs command, QVariant value, bool sub=false) : command(command), req(QDateTime()), reply(QDateTime()), value(value), sub(sub){};

    funcs command;
    QDateTime req;
    QDateTime reply;
    QVariant value;
    bool sub;
};

class cachingQueue : public QThread
{
    Q_OBJECT

signals:
    void haveCommand(funcs func, QVariant param, bool sub);
    void sendValue(cacheItem item);

public slots:
    // Can be called directly or via emit.
    void receiveValue(funcs func, QVariant value, bool sub);

private:

    static cachingQueue *instance;
    static QMutex instanceMutex;

    QMutex mutex;

    QMultiMap <queuePriority,queueItem> queue;
    QMultiMap<funcs,cacheItem> cache;
    QQueue<cacheItem> items;

    // Command to set cache value
    void setCache(funcs func, QVariant val, bool sub=false);
    queuePriority isRecurring(funcs func, bool sub=false);


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
    void add(queuePriority prio ,funcs func, bool recurring=false, bool sub=false);
    void add(queuePriority prio,queueItem item);
    void addUnique(queuePriority prio ,funcs func, bool recurring=false, bool sub=false);
    void addUnique(queuePriority prio,queueItem item);
    void del(funcs func, bool sub=false);
    void clear();
    void interval(quint64 val);
    void updateCache(bool reply, queueItem item);
    void updateCache(bool reply, funcs func, QVariant value=QVariant(), bool sub=false);

    cacheItem getCache(funcs func, bool sub=false);

    QMultiMap<funcs,cacheItem> getCacheItems();
    QMultiMap <queuePriority,queueItem> getQueueItems();
    void unlockMutex();
};

Q_DECLARE_METATYPE(queueItemType)
Q_DECLARE_METATYPE(queueItem)
Q_DECLARE_METATYPE(cacheItem)
#endif // CACHINGQUEUE_H
