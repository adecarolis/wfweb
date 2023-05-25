/*
 *  This is a singleton queue (only 1 instance can be created)
 *
 *  Although subclassing QThread is often not recommended, I think that this is
 *  a valid use case for subclassing.
 *
*/
#include "logcategories.h"
#include "cachingqueue.h"

cachingQueue* cachingQueue::instance{};
QMutex cachingQueue::instanceMutex;

cachingQueue *cachingQueue::getInstance(QObject* parent)
{
    QMutexLocker locker(&instanceMutex);
    if (instance == Q_NULLPTR)
    {
        instance = new cachingQueue(parent);
        instance->setObjectName(("Command Queue"));
        connect (instance, SIGNAL(finished()),instance, SLOT(deleteLater()));
        instance->start(QThread::HighPriority);
    }
    qInfo() << "Returning instance of cachingQueue() to calling process:" << ((parent != Q_NULLPTR) ? parent->objectName(): "<unknown>");
    return instance;
}

cachingQueue::~cachingQueue()
{
    aborted = true;
    waiting.wakeOne(); // wake the thread and signal it to exit.
    qInfo() << "Destroying caching queue (parent closing)";
}

void cachingQueue::run()
{
    // Setup queue.
    qInfo() << "Starting caching queue handler thread (ThreadId:" << QThread::currentThreadId() << ")";

    QElapsedTimer timer;
    timer.start();

    QDeadlineTimer deadline(queueInterval);
    QMutexLocker locker(&mutex);
    quint64 counter=0; // Will run for many years!
    while (!aborted)
    {
        if (!waiting.wait(&mutex, deadline.remainingTime()))
        {
            // Time to process the queue - mutex is locked
            queuePriority prio = priorityImmediate;

            // priorityNone=0, priorityImmediate=1, priorityHighest=2, priorityHigh=3, priorityMediumHigh=5, priorityMedium=7, priorityMediumLow=11, priorityLow=19, priorityLowest=23

            // If no immediate commands then process the rest of the queue
            if (!queue.contains(prio)) {
                if (counter % priorityHighest == 0)
                    prio = priorityHighest;
                else if (counter % priorityHigh == 0)
                    prio = priorityHigh;
                else if (counter % priorityMediumHigh == 0)
                    prio = priorityMediumHigh;
                else if (counter % priorityMedium == 0)
                    prio = priorityMedium;
                else if (counter % priorityMediumLow == 0)
                    prio = priorityMediumLow;
                else if (counter % priorityLow == 0)
                    prio = priorityLow;
                else if (counter % priorityLowest == 0)
                    prio = priorityLowest;
            }            
            counter++;
            QMutableMultiMapIterator<queuePriority,queueItem> itt(queue);

            auto it = queue.find(prio);
            if (it != queue.end())
            {
                auto item = it.value();
                emit haveCommand(item.type, item.command,item.param);
                it = queue.erase(it);
                it = queue.end();
                //if (it != queue.end()) {
                //    while (it != queue.end() && it.key()==prio)
                //        it++;
                //}
                // Add it back into the queue
                if (item.recurring) {
                    queue.insert(it,prio,item);
                    updateCache(false,item.command);
                }
            }

            QCoreApplication::processEvents();
            deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency

        }
        else if (!aborted) {
            // Mutex is locked
            while (!items.isEmpty()) {
                emit sendValue(items.dequeue());
            }
        }
    }
}

void cachingQueue::interval(quint64 val)
{
    this->queueInterval = val;
    waiting.wakeOne();
    qInfo() << "Changing queue interval to" << val << "ms";
}

void cachingQueue::add(queuePriority prio ,funcs func, bool recurring)
{
    queueItem q(func,recurring);
    add(prio,q);
}

void cachingQueue::add(queuePriority prio ,queueItem item)
{
    if (item.command != funcNone)
    {
        QMutexLocker locker(&mutex);
        if (!item.recurring || isRecurring(item.command) != prio)
        {
            if (item.recurring && prio == queuePriority::priorityImmediate) {
                qWarning() << "Warning, cannot add recurring command with immediate priority!" << funcString[item.command];
            } else {
                queue.insert(prio, item);
                updateCache(false,item.command,item.param);
                // Update cache with sent data (will be replaced if found to be invalid.)
                if (item.recurring) qInfo() << "adding" << funcString[item.command] << "recurring" << item.recurring << "priority" << prio;
            }
        }
    }
}

void cachingQueue::addUnique(queuePriority prio ,funcs func, bool recurring)
{
    queueItem q(func,recurring);
    addUnique(prio,q);
}



void cachingQueue::addUnique(queuePriority prio ,queueItem item)
{
    if (item.command != funcNone)
    {
        QMutexLocker locker(&mutex);
        if (!item.recurring || isRecurring(item.command) != prio)
        {
            if (item.recurring && prio == queuePriority::priorityImmediate) {
                qWarning() << "Warning, cannot add unique recurring command with immediate priority!" << funcString[item.command];
            } else {
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0))
                queue.erase(std::remove_if(queue.begin(), queue.end(), [item](const queueItem& c) {  return (c.command == item.command && c.recurring == item.recurring); }), queue.end());
#else
                auto it(queue.begin());
                while (it != queue.end()) {
                    if (it.value().command == item.command && it.value().recurring == item.recurring)
                        it = queue.erase(it);
                    else
                        it++;
                }
#endif
                queue.insert(prio, item);
                updateCache(false,item.command,item.param);
                if (item.recurring) qInfo() << "adding" << funcString[item.command] << "recurring" << item.recurring << "priority" << prio;
            }
        }
    }
}

void cachingQueue::del(funcs func)
{
    QMutexLocker locker(&mutex);
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0))
    queue.erase(std::remove_if(queue.begin(), queue.end(), [func](const queueItem& c) {  return (c.command == func); }), queue.end());
#else
    auto it(queue.begin());
    while (it != queue.end()) {
        if (it.value().command == func)
            it = queue.erase(it);
        else
            it++;
    }
#endif
    qInfo() << "deleting" << funcString[func];
}

queuePriority cachingQueue::isRecurring(funcs func)
{
    // Does NOT lock the mutex
    auto rec = std::find_if(queue.begin(), queue.end(), [func](const queueItem& c) {  return (c.command == func && c.recurring); });
    if (rec != queue.end())
    {
        return rec.key();
    }
    return queuePriority::priorityNone;
}

void cachingQueue::clear()
{
    QMutexLocker locker(&mutex);
    queue.clear();
}
void cachingQueue::message(QString msg)
{
    qInfo() << "Received:" << msg;
    waiting.wakeOne();
}

void cachingQueue::receiveValue(funcs func, QVariant value, bool sub)
{
    QMutexLocker locker(&mutex);
    cacheItem c = cacheItem(func,value);
    items.enqueue(c);
    updateCache(true,func,value);
    waiting.wakeOne();
}

void cachingQueue::updateCache(bool reply, funcs func, QVariant value)
{
 /*   if (func == funcFreqGet || func == funcFreqTR || func == funcSelectedFreq || func == funcUnselectedFreq)
        func = funcVFOFrequency; // This is a composite function.
    if (func == funcModeGet || func == funcModeTR || func == funcSelectedMode || func == funcUnselectedMode)
        func = funcVFOMode; // This is a composite function. */

    // Mutex MUST be locked by the calling function.
    auto cv = cache.find(func);
    if (cv != cache.end()) {
        if (reply) {
            cv->reply = QDateTime::currentDateTime();
        } else {
            cv->req = QDateTime::currentDateTime();
        }
        // If we are sending an actual value, update the cache with it
        // Value will be replaced if invalid on next get()
        if (value.isValid())
            cv->value = value;

        return;
    }

    cacheItem c;
    c.command = func;
    if (reply) {
        c.reply = QDateTime::currentDateTime();
    } else {
        c.req = QDateTime::currentDateTime();
    }
    // If we are sending an actual value, update the cache with it
    // Value will be replaced if invalid on next get()
    if (value.isValid())
        c.value = value;
    cache.insert(func,c);
}


cacheItem cachingQueue::getCache(funcs func)
{
    cacheItem ret;
    if (func != funcNone) {
        QMutexLocker locker(&mutex);
        auto it = cache.find(func);
        if (it != cache.end())
            ret = cacheItem(*it);
    }
    // If the cache is more than 5-20 seconds old, re-request it as it may be stale (maybe make this a config option?)
    // Using priorityhighest WILL slow down the S-Meter when a command intensive client is connected to rigctl
    if (func != funcNone && (!ret.value.isValid() || ret.reply.addSecs(QRandomGenerator::global()->bounded(5,20)) <= QDateTime::currentDateTime())) {
        //qInfo() << "No (or expired) cache found for" << funcString[func] << "requesting";
        add(priorityHighest,func);
    }
    return ret;
}

//Calling function MUST call unlockMutex() once finished with data
QMap<funcs,cacheItem> cachingQueue::getCacheItems()
{
    mutex.lock();
    return cache;
}

//Calling function MUST call unlockMutex() once finished with data
QMultiMap <queuePriority,queueItem> cachingQueue::getQueueItems()
{
    mutex.lock();
    return queue;
}

void cachingQueue::unlockMutex()
{
    mutex.unlock();
}
