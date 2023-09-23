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

            auto it = queue.find(prio);
            if (it != queue.end())
            {
                auto item = it.value();
                emit haveCommand(item.command,item.param,item.sub);
                it=queue.erase(it);
                if (item.recurring) {
                    while (it.key() == prio)
                    {
                        it++;
                    }
                    queue.insert(--it,prio,item);
                    updateCache(false,item.command,item.param,item.sub);
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

void cachingQueue::add(queuePriority prio ,funcs func, bool recurring, bool sub)
{
    queueItem q(func,recurring,sub);
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
                if (item.recurring) {
                    // also insert an immediate command to get the current value "now" (removes the need to get rigstate)
                    queueItem it=item;
                    it.recurring=false;
                    queue.insert(queue.cend(),priorityHighest, it);
                    qInfo() << "adding" << funcString[item.command] << "recurring" << item.recurring << "priority" << prio << "sub" << item.sub;
                }
                queue.insert(queue.cend(),prio, item);
                // Update cache with sent data (will be replaced if found to be invalid.)
                updateCache(false,item.command,item.param,item.sub);
            }
        }
    }
}

void cachingQueue::addUnique(queuePriority prio ,funcs func, bool recurring, bool sub)
{
    queueItem q(func,recurring, sub);
    addUnique(prio,q);
}



void cachingQueue::addUnique(queuePriority prio ,queueItem item)
{
    if (item.command != funcNone)
    {
        QMutexLocker locker(&mutex);
        if (item.recurring && prio == queuePriority::priorityImmediate) {
            qWarning() << "Warning, cannot add unique recurring command with immediate priority!" << funcString[item.command];
        } else {
            auto it(queue.begin());
            // This is quite slow but a new unique command is only added in response to user interaction (mode change etc.)
            while (it != queue.end()) {
                if (it.value().command == item.command && it.value().recurring == item.recurring && it.value().sub == item.sub && it.value().param.isValid() == item.param.isValid())
                {
                    qInfo() << "deleting" << it.value().id << funcString[it.value().command] << "sub" << it.value().sub << "recurring" << it.value().recurring ;
                    it = queue.erase(it);
                }
                else
                {
                    it++;
                }
            }
            if (item.recurring) {
                // also insert an immediate command to get the current value "now" (removes the need to get initial rigstate)
                queueItem it = item;
                it.recurring=false;
                queue.insert(queue.cend(),priorityHighest, it);
                qInfo() << "adding unique" << funcString[item.command] << "recurring" << item.recurring << "priority" << prio << "sub" << item.sub;
            }
            queue.insert(queue.cend(),prio, item);
            updateCache(false,item.command,item.param,item.sub);
        }
    }
}

void cachingQueue::del(funcs func, bool sub)
{
    // This will immediately delete any matching commands.
    if (func != funcNone)
    {
        QMutexLocker locker(&mutex);
        auto it = std::find_if(queue.begin(), queue.end(), [func,sub](const queueItem& c) {  return (c.command == func && c.sub == sub && c.recurring); });
        //auto it(queue.begin());
        if (it == queue.end())
            qInfo() << "recurring command" << funcString[func] << "sub" << sub << "not found in queue";
        while (it != queue.end()) {
            if (it.value().command == func && it.value().sub == sub) {
                    qInfo() << "deleting" << funcString[it.value().command] << "sub" << it.value().sub << "recurring" << it.value().recurring;
                it = queue.erase(it);
            }
            else
            {
                it++;
            }
        }
    }
}

queuePriority cachingQueue::isRecurring(funcs func, bool sub)
{
    // Does NOT lock the mutex
    auto rec = std::find_if(queue.begin(), queue.end(), [func,sub](const queueItem& c) {  return (c.command == func && c.sub == sub && c.recurring); });
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
    cacheItem c = cacheItem(func,value,sub);
    items.enqueue(c);
    updateCache(true,func,value,sub);
    waiting.wakeOne();
}


void cachingQueue::updateCache(bool reply, queueItem item)
{
    // Mutex MUST be locked by the calling function.
    auto cv = cache.find(item.command);
    while (cv != cache.end() && cv->command == item.command) {
        if (cv->sub == item.sub) {
            if (reply) {
                cv->reply = QDateTime::currentDateTime();
            } else {
                cv->req = QDateTime::currentDateTime();
            }
            // If we are sending an actual value, update the cache with it
            // Value will be replaced if invalid on next get()

            if (compare(item.param,cv.value().value))
            {
                cv->value = item.param;
                emit cacheUpdated(cv.value());
            }
            return;
            // We have found (and updated) a matching item so return
        }
        ++cv;
    }

    cacheItem c;
    c.command = item.command;
    c.sub = item.sub;
    if (reply) {
        c.reply = QDateTime::currentDateTime();
    } else {
        c.req = QDateTime::currentDateTime();
    }
    // If we are sending an actual value, update the cache with it
    // Value will be replaced if invalid on next get()
    if (item.param.isValid())
        c.value = item.param;
    cache.insert(item.command,c);
}


void cachingQueue::updateCache(bool reply, funcs func, QVariant value, bool sub)
{
    queueItem q(func,value,false,sub);
    updateCache(reply,q);
}


cacheItem cachingQueue::getCache(funcs func, bool sub)
{
    cacheItem ret;
    if (func != funcNone) {
        QMutexLocker locker(&mutex);
        auto it = cache.find(func);
        while (it != cache.end() && it->command == func)
        {
            if (it->sub == sub)
                ret = cacheItem(*it);
            ++it;
        }
    }
    // If the cache is more than 5-20 seconds old, re-request it as it may be stale (maybe make this a config option?)
    // Using priorityhighest WILL slow down the S-Meter when a command intensive client is connected to rigctl
    if (func != funcNone && (!ret.value.isValid() || ret.reply.addSecs(QRandomGenerator::global()->bounded(5,20)) <= QDateTime::currentDateTime())) {
        //qInfo() << "No (or expired) cache found for" << funcString[func] << "requesting";
        add(priorityHighest,func,false,sub);
    }
    return ret;
}

//Calling function MUST call unlockMutex() once finished with data
QMultiMap<funcs,cacheItem> cachingQueue::getCacheItems()
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

bool cachingQueue::compare(QVariant a, QVariant b)
{
    bool changed = false;
    if (a.isValid() && b.isValid()) {
        // Compare the details
        if (!strcmp(a.typeName(),"bool")){
            if (a.value<bool>() != b.value<bool>())
                changed=true;
        } else if (!strcmp(a.typeName(),"QString")) {
            if (a.value<QString>() != b.value<QString>())
                changed=true;
        } else if (!strcmp(a.typeName(),"uchar")) {
            if (a.value<uchar>() != b.value<uchar>())
                changed=true;
        } else if (!strcmp(a.typeName(),"ushort")) {
            if (a.value<ushort>() != b.value<ushort>())
                changed=true;
        } else if (!strcmp(a.typeName(),"short")) {
            if (a.value<short>() != a.value<short>())
                changed=true;
        } else if (!strcmp(a.typeName(),"uint")) {
            if (a.value<uint>() != b.value<uint>())
            changed=true;
        } else if (!strcmp(a.typeName(),"modeInfo")) {
            if (a.value<modeInfo>().mk != b.value<modeInfo>().mk || a.value<modeInfo>().reg != b.value<modeInfo>().reg
                         || a.value<modeInfo>().bw != b.value<modeInfo>().bw || a.value<modeInfo>().filter != b.value<modeInfo>().filter) {
                changed=true;
            }
        } else if(!strcmp(a.typeName(),"freqt")) {
            if (a.value<freqt>().Hz != b.value<freqt>().Hz)
                changed=true;
        } else if(!strcmp(a.typeName(),"antennaInfo")) {
            if (a.value<antennaInfo>().antenna != b.value<antennaInfo>().antenna || a.value<antennaInfo>().rx != b.value<antennaInfo>().rx)
                changed=true;
        } else if(!strcmp(a.typeName(),"rigInput")) {
            if (a.value<rigInput>().type != b.value<rigInput>().type)
                changed=true;
        } else if (!strcmp(a.typeName(),"duplexMode_t")) {
            if (a.value<duplexMode_t>() != b.value<duplexMode_t>())
                changed=true;
        } else if (!strcmp(a.typeName(),"spectrumMode_t")) {
            if (a.value<spectrumMode_t>() != b.value<spectrumMode_t>())
                changed=true;
        } else if (!strcmp(a.typeName(),"centerSpanData")) {
            if (a.value<centerSpanData>().cstype != b.value<centerSpanData>().cstype || a.value<centerSpanData>().freq != b.value<centerSpanData>().freq  )
                changed=true;
        } else if (!strcmp(a.typeName(),"scopeData") || !strcmp(a.typeName(),"memoryType") || !strcmp(a.typeName(),"bandStackType") ) {
            changed=true; // Always different
        } else {
            // Maybe Try simple comparison?
            qInfo () << "Unsupported cache value:" << a.typeName();
        }
    } else if (a.isValid()) {
        changed = true;
    }

    return changed;
}

