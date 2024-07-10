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

inline QMap<QString,int> priorityMap = {{"None",0},{"Immediate",1},{"Highest",2},{"High",3},{"Medium High",5},{"Medium",7},{"Medium Low",11},{"Low",19},{"Lowest",23}};

// Command with no param is a get by default
struct queueItem {
    queueItem () {}
    queueItem (funcs command, QVariant param, bool recurring, uchar receiver) : command(command), param(param), receiver(receiver), recurring(recurring){};
    queueItem (funcs command, QVariant param, bool recurring) : command(command), param(param), receiver(false), recurring(recurring){};
    queueItem (funcs command, QVariant param) : command(command), param(param),receiver(0), recurring(false){};
    queueItem (funcs command, bool recurring, uchar receiver) : command(command), param(QVariant()), receiver(receiver), recurring(recurring) {};
    queueItem (funcs command, bool recurring) : command(command), param(QVariant()), receiver(0), recurring(recurring) {};
    queueItem (funcs command) : command(command), param(QVariant()), receiver(0), recurring(false){};
    funcs command;
    QVariant param;
    uchar receiver;
    bool recurring;
    qint64 id = QDateTime::currentMSecsSinceEpoch();
    bool operator==(const queueItem& lhs)
    {
        return (lhs.command == command && lhs.receiver == receiver && lhs.recurring == recurring);
    }
};

struct cacheItem {
    cacheItem () {};
    cacheItem (funcs command, QVariant value, uchar receiver=0) : command(command), req(QDateTime()), reply(QDateTime()), value(value), receiver(receiver){};

    funcs command;
    QDateTime req;
    QDateTime reply;
    QVariant value;
    uchar receiver;
};

class cachingQueue : public QThread
{
    Q_OBJECT

signals:
    void haveCommand(funcs func, QVariant param, uchar receiver);
    void sendValue(cacheItem item);
    void sendMessage(QString msg);
    void cacheUpdated(cacheItem item);
    void rigCapsUpdated(rigCapabilities* caps);

public slots:
    // Can be called directly or via emit.
    void receiveValue(funcs func, QVariant value, uchar receiver);

private:

    static cachingQueue *instance;
    static QMutex instanceMutex;

    QMutex mutex;

    QMultiMap <queuePriority,queueItem>* queue = Q_NULLPTR;
    QMultiMap<funcs,cacheItem>* cache = Q_NULLPTR;
    QQueue<cacheItem>* items = Q_NULLPTR;
    QQueue<QString>* messages = Q_NULLPTR;

    // Command to set cache value
    void setCache(funcs func, QVariant val, uchar receiver=0);
    queuePriority isRecurring(funcs func, uchar receiver=0);
    bool compare(QVariant a, QVariant b);


    // Various other values
    bool aborted=false;
    QWaitCondition* waiting = Q_NULLPTR;
    qint64 queueInterval=-1; // Don't start the timer!
    
    rigCapabilities* rigCaps = Q_NULLPTR; // Must be NULL until a radio is connected
    
    // Functions
    void run();
    funcs checkCommandAvailable(funcs cmd, bool set=false);

protected:
    cachingQueue(QObject* parent = Q_NULLPTR) : QThread(parent) {};
    ~cachingQueue();

public:
    cachingQueue(cachingQueue &other) = delete;
    void operator=(const cachingQueue &) = delete;

    static cachingQueue *getInstance(QObject* parent = Q_NULLPTR);
    void message(QString msg);
    void add(queuePriority prio ,funcs func, bool recurring=false, uchar receiver=0);
    void add(queuePriority prio,queueItem item);
    void addUnique(queuePriority prio ,funcs func, bool recurring=false, uchar receiver=0);
    void addUnique(queuePriority prio,queueItem item);
    void del(funcs func, uchar receiver=0);
    void clear();
    void interval(qint64 val);
    qint64 interval() {return queueInterval;}
    void updateCache(bool reply, queueItem item);
    void updateCache(bool reply, funcs func, QVariant value=QVariant(), uchar receiver=0);

    cacheItem getCache(funcs func, uchar receiver=0);

    QMultiMap <funcs,cacheItem>* getCacheItems();
    QMultiMap <queuePriority,queueItem>* getQueueItems();
    void lockMutex() {mutex.lock();}
    void unlockMutex() {mutex.unlock();}
    void setRigCaps(rigCapabilities* caps) { if (rigCaps != caps) { rigCaps = caps; emit rigCapsUpdated(rigCaps);} }
    rigCapabilities* getRigCaps() { return rigCaps; }
};

Q_DECLARE_METATYPE(queueItem)
Q_DECLARE_METATYPE(cacheItem)
#endif // CACHINGQUEUE_H
