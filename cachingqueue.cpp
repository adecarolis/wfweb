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
        instance->setObjectName(("cachingQueue()"));
        connect (instance, SIGNAL(finished()),instance, SLOT(deleteLater()));
        instance->start(QThread::TimeCriticalPriority);
    }
    qDebug() << "Returning instance of cachingQueue() to calling process:" << ((parent != Q_NULLPTR) ? parent->objectName(): "<unknown>");
    return instance;
}

cachingQueue::~cachingQueue()
{
    aborted = true;
    if (queue != Q_NULLPTR) {
        waiting->wakeOne(); // wake the thread and signal it to exit.
    }
    qInfo() << "Destroying caching queue (parent closing)";
}

void cachingQueue::run()
{
    // Setup queue.
    qInfo() << "Starting caching queue handler thread (ThreadId:" << QThread::currentThreadId() << ")";

    if (queue == Q_NULLPTR){
        queue = new QMultiMap <queuePriority,queueItem>;
    }

    if (cache == Q_NULLPTR){
        cache = new QMultiMap<funcs,cacheItem>;
    }

    if (items == Q_NULLPTR){
        items = new QQueue<cacheItem>;
    }

    if (messages == Q_NULLPTR){
        messages = new QQueue<QString>;
    }

    if (waiting == Q_NULLPTR) {
        waiting = new QWaitCondition();
    }

    QElapsedTimer timer;
    timer.start();

    QDeadlineTimer deadline(queueInterval);
    QMutexLocker locker(&mutex);
    quint64 counter=0; // Will run for many years!
    while (!aborted)
    {
        if (!waiting->wait(&mutex, deadline.remainingTime()))
        {
            // Time to process the queue - mutex is locked
            queuePriority prio = priorityImmediate;

            // priorityNone=0, priorityImmediate=1, priorityHighest=2, priorityHigh=3, priorityMediumHigh=5, priorityMedium=7, priorityMediumLow=11, priorityLow=19, priorityLowest=23

            // If no immediate commands then process the rest of the queue
            if (!queue->contains(prio)) {
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

            //auto it = queue.upperBound(prio);
            //it--; //upperBound returns the item immediately following the last key.
            //if (it != queue.end() && it.key() == prio)
            auto it = queue->find(prio);
            if (it != queue->end()) {
                while (it != queue->end() && it.key() == prio)
                {
                    it++;
                }
                it--;
                auto item = it.value();
                emit haveCommand(item.command,item.param,item.receiver);
                //it=queue.erase(it);
                queue->remove(prio,it.value());
                if (item.recurring && prio != priorityImmediate) {
                    queue->insert(prio,item);
                }
                updateCache(false,item.command,item.param,item.receiver);
            }
            deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency

            QCoreApplication::processEvents();

        }
        else if (!aborted) {
            // Mutex is locked
            while (!items->isEmpty()) {
                emit sendValue(items->dequeue());
            }
            while (!messages->isEmpty()) {
                emit sendMessage(messages->dequeue());
            }
            if (queueInterval != -1 && deadline.isForever())
                deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency
        }
    }

    if (waiting != Q_NULLPTR) {
        delete waiting;
        waiting = Q_NULLPTR;
    }

    if (queue != Q_NULLPTR){
        delete queue;
        queue = Q_NULLPTR;
    }

    if (cache != Q_NULLPTR){
        delete cache;
        cache = Q_NULLPTR;
    }

    if (items != Q_NULLPTR){
        delete items;
        items = Q_NULLPTR;
    }

    if (messages != Q_NULLPTR){
        delete messages;
        messages = Q_NULLPTR;
    }

}

void cachingQueue::interval(qint64 val)
{
    this->queueInterval = val;
    if (queue != Q_NULLPTR) {
        waiting->wakeAll();
    }
    qInfo() << "Changing queue interval to" << val << "ms";
}

funcs cachingQueue::checkCommandAvailable(funcs cmd,bool set)
{

    // If we don't have rigCaps yet, simply return the command.
    if (rigCaps != Q_NULLPTR && cmd != funcNone && !rigCaps->commands.contains(cmd)) {
        // We don't have the requested command, so lets see if we can change it to something we do have.
        // WFVIEW functions should use funcMain/Sub commands by default,
        if (cmd == funcMainFreq && rigCaps->commands.contains(funcSelectedFreq))
            cmd = funcSelectedFreq;
        else if (cmd == funcSubFreq && rigCaps->commands.contains(funcUnselectedFreq))
            cmd = funcSelectedFreq;
        else if (cmd == funcMainMode && rigCaps->commands.contains(funcSelectedMode))
            cmd = funcSelectedMode;
        else if (cmd == funcSubMode && rigCaps->commands.contains(funcUnselectedMode))
            cmd = funcUnselectedFreq;
        // These are fallback commands for older radios that don't have command 25/26
        else if(cmd == funcMainMode)
        {
            if (set)
                cmd = funcModeSet;
            else
                cmd = funcModeGet;
        }
        else if(cmd == funcMainFreq)
        {
            if (set)
                cmd = funcFreqSet;
            else
                cmd = funcFreqGet;
        }
        else
            cmd = funcNone;
    }
    return cmd;
}

void cachingQueue::add(queuePriority prio ,funcs func, bool recurring, uchar receiver)
{
    if (queue != Q_NULLPTR) {
        queueItem q(func,recurring,receiver);
        add(prio,q);
    }
}

void cachingQueue::add(queuePriority prio ,queueItem item)
{
    if (queue != Q_NULLPTR) {
        item.command=checkCommandAvailable(item.command,item.param.isValid());
        if (item.command != funcNone)
        {
            QMutexLocker locker(&mutex);
            if (!item.recurring || isRecurring(item.command,item.receiver) != prio)
            {
                queueItem it=item;
                if (item.recurring && prio == queuePriority::priorityImmediate) {
                    qWarning() << "Warning, cannot add recurring command with immediate priority!" << funcString[item.command];
                } else {
                    if (it.recurring) {
                        // also insert an immediate command to get the current value "now" (removes the need to get rigstate)
                        it.recurring=false;
                        it.param.clear();
                        queue->insert(queue->cend(),priorityImmediate, it);
                        qDebug() << "adding" << funcString[item.command] << "recurring" << it.recurring << "priority" << prio << "receiver" << it.receiver;
                    }
                    queue->insert(prio, it);
                }
            }
        }
    }
}


void cachingQueue::addUnique(queuePriority prio ,funcs func, bool recurring, uchar receiver)
{
    if (queue != Q_NULLPTR) {
        queueItem q(func,recurring, receiver);
        addUnique(prio,q);
    }
}



void cachingQueue::addUnique(queuePriority prio ,queueItem item)
{
    if (queue != Q_NULLPTR) {
        item.command=checkCommandAvailable(item.command,item.param.isValid());
        if (item.command != funcNone)
        {
            QMutexLocker locker(&mutex);
            if (item.recurring && prio == queuePriority::priorityImmediate) {
                qWarning() << "Warning, cannot add unique recurring command with immediate priority!" << funcString[item.command];
            } else {
                int count=queue->remove(prio,item);
                if (count>0)
                    qDebug() << "cachingQueue()::addUnique deleted" << count << "entries from queue for" << funcString[item.command] << "on receiver" << item.receiver;

                if (item.recurring) {
                    // also insert an immediate command to get the current value "now" (removes the need to get initial rigstate)
                    queueItem it = item;
                    it.recurring=false;
                    it.param.clear();
                    queue->insert(queue->cend(),priorityImmediate, it);
                    qDebug() << "adding unique" << funcString[item.command] << "recurring" << item.recurring << "priority" << prio << "receiver" << item.receiver;
                }
                queue->insert(prio, item);
            }
        }
    }
}

void cachingQueue::del(funcs func, uchar receiver)
{
    // This will immediately delete any matching commands.
    if (queue != Q_NULLPTR && func != funcNone)
    {
        QMutexLocker locker(&mutex);
        auto it = std::find_if(queue->begin(), queue->end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
        //auto it(queue.begin());
        if (it != queue->end()) {
            int count = queue->remove(it.key(),it.value());
            if (count>0)
                qDebug() << "cachingQueue()::del" << count << "entries from queue for" << funcString[func] << "on receiver" << receiver;
        }
    }
}

queuePriority cachingQueue::isRecurring(funcs func, uchar receiver)
{
    // Does NOT lock the mutex
    if (queue != Q_NULLPTR) {
        auto rec = std::find_if(queue->begin(), queue->end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
        if (rec != queue->end())
        {
            return rec.key();
        }
    }
    return queuePriority::priorityNone;
}

void cachingQueue::clear()
{
    if (queue != Q_NULLPTR) {
        QMutexLocker locker(&mutex);
        queue->clear();
    }
}
void cachingQueue::message(QString msg)
{
    if (messages != Q_NULLPTR) {
        QMutexLocker locker(&mutex);
        messages->append(msg);
        qDebug() << "Received:" << msg;
        waiting->wakeOne();
    }
}

void cachingQueue::receiveValue(funcs func, QVariant value, uchar receiver)
{
    if (cache != Q_NULLPTR) {
        QMutexLocker locker(&mutex);
        cacheItem c = cacheItem(func,value,receiver);
        items->enqueue(c);
        updateCache(true,func,value,receiver);
        waiting->wakeOne();
    }
}


void cachingQueue::updateCache(bool reply, queueItem item)
{
    // Mutex MUST be locked by the calling function.
    if (cache != Q_NULLPTR) {
        auto cv = cache->find(item.command);
        while (cv != cache->end() && cv->command == item.command) {
            if (cv->receiver == item.receiver) {
                if (reply) {
                    cv->reply = QDateTime::currentDateTime();
                } else {
                    cv->req = QDateTime::currentDateTime();
                }
                // If we are sending an actual value, update the cache with it
                // Value will be replaced if invalid on next get()

                if (compare(item.param,cv.value().value))
                {
                    cv->value.clear();
                    cv->value.setValue(item.param);

                    emit cacheUpdated(cv.value());
                }
                return;
                // We have found (and updated) a matching item so return
            }
            ++cv;
        }

        cacheItem c;
        c.command = item.command;
        c.receiver = item.receiver;
        if (reply) {
            c.reply = QDateTime::currentDateTime();
        } else {
            c.req = QDateTime::currentDateTime();
        }
        // If we are sending an actual value, update the cache with it
        // Value will be replaced if invalid on next get()
        if (item.param.isValid()) {
            c.value = item.param;
        }
        cache->insert(item.command,c);
    }
}


void cachingQueue::updateCache(bool reply, funcs func, QVariant value, uchar receiver)
{
    if (cache != Q_NULLPTR) {
        queueItem q(func,value,false,receiver);
        updateCache(reply,q);
    }
}


cacheItem cachingQueue::getCache(funcs func, uchar receiver)
{
    cacheItem ret;
    if (cache != Q_NULLPTR && func != funcNone) {
        QMutexLocker locker(&mutex);
        auto it = cache->find(func);
        while (it != cache->end() && it->command == func)
        {
            if (it->receiver == receiver)
                ret = cacheItem(*it);
            ++it;
        }
    }
    // If the cache is more than 5-20 seconds old, re-request it as it may be stale (maybe make this a config option?)
    // Using priorityhighest WILL slow down the S-Meter when a command intensive client is connected to rigctl
    if (func != funcNone && (!ret.value.isValid() || ret.reply.addSecs(QRandomGenerator::global()->bounded(5,20)) <= QDateTime::currentDateTime())) {
        //qInfo() << "No (or expired) cache found for" << funcString[func] << "requesting";
        add(priorityHighest,func,false,receiver);
    }
    return ret;
}

//Calling function MUST call unlockMutex() once finished with data
QMultiMap<funcs,cacheItem>* cachingQueue::getCacheItems()
{
    mutex.lock();
    return cache;
}

//Calling function MUST call unlockMutex() once finished with data
QMultiMap <queuePriority,queueItem>* cachingQueue::getQueueItems()
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
        } else if (!strcmp(a.typeName(),"int")) {
            if (a.value<int>() != b.value<int>())
                changed=true;
        } else if (!strcmp(a.typeName(),"modeInfo")) {
            if (a.value<modeInfo>().mk != b.value<modeInfo>().mk || a.value<modeInfo>().reg != b.value<modeInfo>().reg
                || a.value<modeInfo>().filter != b.value<modeInfo>().filter || a.value<modeInfo>().data != b.value<modeInfo>().data) {
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
        } else if (!strcmp(a.typeName(),"toneInfo")) {
            if (a.value<toneInfo>().tone != b.value<toneInfo>().tone)
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

