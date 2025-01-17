/*
 *  This is a singleton queue (only 1 instance can be created)
 *
 *  Although subclassing QThread is often not recommended, I think that this is
 *  a valid use case for subclassing.
 *
*/
#include "logcategories.h"
#include "cachingqueue.h"

#define CACHE_LOCK_TIME 100

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
    if (mutex.tryLock(CACHE_LOCK_TIME)) {
        mutex.unlock();
        waiting.wakeOne(); // wake the thread and signal it to exit.
    }
    else {
        qWarning(logRig()) << "Failed to delete cachingQueue() after" << CACHE_LOCK_TIME << "ms, mutex locked";
    }
    qInfo() << "Destroying caching queue (parent closing)";
}

void cachingQueue::run()
{
    // Setup queue.
    qInfo() << "Starting caching queue handler thread (ThreadId:" << QThread::currentThreadId() << ")";

    QDeadlineTimer deadline(queueInterval);
    QMutexLocker locker(&mutex);

    quint16 counter=priorityImmediate;

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
                else if (counter % priorityLowest == 0) {
                    prio = priorityLowest;
                    counter = priorityImmediate;
                }
            }
            counter++;

            //auto it = queue.upperBound(prio);
            //it--; //upperBound returns the item immediately following the last key.
            //if (it != queue.end() && it.key() == prio)
            auto it = queue.find(prio);
            if (it != queue.end()) {
                while (it != queue.end() && it.key() == prio)
                {
                    it++;
                }
                it--;
                auto item = it.value();
                emit haveCommand(item.command,item.param,item.receiver);
                it=queue.erase(it);
                //queue.remove(prio,it.value()); // Will remove ALL matching commands which breaks some things (memory bulk write)
                if (item.recurring && prio != priorityImmediate) {
                    queue.insert(prio,item);
                }
                updateCache(false,item.command,item.param,item.receiver);
            }
#ifdef Q_OS_MACOS
            while (!items.isEmpty()) {
                emit sendValue(items.dequeue());
            }
            while (!messages.isEmpty()) {
                emit sendMessage(messages.dequeue());
            }
#else
            QCoreApplication::processEvents();
#endif
            deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency

        }
        else if (!aborted) {
            // Mutex is locked
            while (!items.isEmpty()) {
                emit sendValue(items.dequeue());
            }
            while (!messages.isEmpty()) {
                emit sendMessage(messages.dequeue());
            }
            if (queueInterval != -1 && deadline.isForever())
                deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency
        }
    }
}

void cachingQueue::interval(qint64 val)
{
    this->queueInterval = val;
    if (mutex.tryLock()) {
        mutex.unlock();
        waiting.wakeAll();
    }
    emit intervalUpdated(val);
    qInfo() << "Changing queue interval to" << val << "ms";
}

funcs cachingQueue::checkCommandAvailable(funcs cmd,bool set)
{
    Q_UNUSED(set)
    if (rigCaps != Q_NULLPTR && cmd != funcNone && cmd != funcSelectVFO && !rigCaps->commands.contains(cmd)) {
        // We don't have the requested command!
        return funcNone;
    }
    return cmd;
}

void cachingQueue::add(queuePriority prio ,funcs func, bool recurring, uchar receiver)
{
    queueItem q(func,recurring,receiver);
    add(prio,q);
}

void cachingQueue::add(queuePriority prio ,queueItem item)
{
    // If the queue isn't running, no point adding to it!
    if (this->queueInterval != -1)
    {
        item.command=checkCommandAvailable(item.command,item.param.isValid());
        if ((item.receiver != 0xff || rigState.receiver == item.receiver) && item.command != funcNone)
        {
            QMutexLocker locker(&mutex);
            if (!item.recurring || isRecurring(item.command,item.receiver) != prio)
            {
                if (item.recurring && prio == queuePriority::priorityImmediate) {
                    qWarning() << "Warning, cannot add recurring command with immediate priority!" << funcString[item.command];
                } else {
                    if (item.recurring) {
                        // also insert an immediate command to get the current value "now" (removes the need to get rigstate)
                        queueItem it=item;
                        it.recurring=false;
                        it.param.clear();
                        //qDebug() << "adding" << funcString[item.command] << "recurring" << it.recurring << "priority" << prio << "receiver" << it.receiver;
                        queue.insert(queue.cend(),priorityImmediate, it);
                    }
                    queue.insert(prio, item);
                }
            }
        }
    }
}


void cachingQueue::addUnique(queuePriority prio ,funcs func, bool recurring, uchar receiver)
{
    queueItem q(func,recurring, receiver);
    addUnique(prio,q);
}



void cachingQueue::addUnique(queuePriority prio ,queueItem item)
{
    // If the queue isn't running, no point adding to it!
    if (this->queueInterval != -1)
    {
        item.command=checkCommandAvailable(item.command,item.param.isValid());
        if ((item.receiver != 0xff || rigState.receiver == item.receiver) && item.command != funcNone)
        {
            QMutexLocker locker(&mutex);
            if (item.recurring && prio == queuePriority::priorityImmediate) {
                qWarning() << "Warning, cannot add unique recurring command with immediate priority!" << funcString[item.command];
            } else {
                int count=queue.remove(prio,item);
                if (count>0)
                    qDebug() << "cachingQueue()::addUnique deleted" << count << "entries from queue for" << funcString[item.command] << "on receiver" << item.receiver;

                if (item.recurring) {
                    // also insert an immediate command to get the current value "now" (removes the need to get initial rigstate)
                    queueItem it = item;
                    it.recurring=false;
                    it.param.clear();
                    if (it.command != funcTransceiverId)
                        queue.insert(queue.cend(),priorityImmediate, it);
                    qDebug() << "adding unique" << funcString[item.command] << "recurring" << item.recurring << "priority" << prio << "receiver" << item.receiver;
                }
                queue.insert(prio, item);
            }
        }
    }
}

vfoCommandType cachingQueue::getVfoCommand(vfo_t vfo,uchar rx, bool set)
{
    vfoCommandType cmd;
    cmd.receiver = rx;
    cmd.vfo = vfo;
    if (rigCaps != Q_NULLPTR) {
        QMutexLocker locker(&mutex);

        if (set) {
            cmd.modeFunc = ((rigCaps->commands.contains(funcMode)) ? funcMode: funcModeSet) ;
            cmd.freqFunc = ((rigCaps->commands.contains(funcFreq)) ? funcFreq: funcFreqSet) ;
        } else {
            cmd.modeFunc = ((rigCaps->commands.contains(funcMode)) ? funcMode: funcModeGet) ;
            cmd.freqFunc = ((rigCaps->commands.contains(funcFreq)) ? funcFreq: funcFreqGet) ;
        }

        if (!rigCaps->hasCommand29) {
            // This radio has no direct access to each receiver (main/sub
            if (rigState.vfoMode == vfoModeType_t::vfoModeVfo && (rigState.receiver == 0 && rx == 0))
            {
                // This is acting on main
                if (vfo == vfoA) {
                    cmd.modeFunc = ((rigCaps->commands.contains(funcSelectedMode)) ? funcSelectedMode: cmd.modeFunc);
                    cmd.freqFunc = ((rigCaps->commands.contains(funcSelectedFreq)) ? funcSelectedFreq: cmd.freqFunc);
                } else if (vfo == vfoB){
                    cmd.modeFunc = ((rigCaps->commands.contains(funcUnselectedMode)) ? funcUnselectedMode: cmd.modeFunc);
                    cmd.freqFunc = ((rigCaps->commands.contains(funcUnselectedFreq)) ? funcUnselectedFreq: cmd.freqFunc);
                }
            }
            else if (rx && rigState.receiver)
            {
                // Requesting receiver is current
                cmd.receiver = 0;
            }
            else
            {
                // Requesting receiver is not current.
                cmd.modeFunc = funcNone;
                cmd.freqFunc = funcNone;
                cmd.receiver = 0xff;
            }
        }
    }
    //qDebug(logRig()) << "Queue VFO:" << vfo << "rx:" << rx<< "set:" << set << "mode:" << funcString[cmd.modeFunc] << "freq:" << funcString[cmd.freqFunc] << "rigstate: vfoMode:" << rigState.vfoMode << "vfo" << rigState.vfo << "rx" << rigState.receiver;
    return cmd;
}

queuePriority cachingQueue::del(funcs func, uchar receiver)
{
    // This will immediately delete any matching commands.
    queuePriority prio = priorityNone;
    if (func != funcNone)
    {
        QMutexLocker locker(&mutex);
        auto it = std::find_if(queue.begin(), queue.end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
        //auto it(queue.begin());
        if (it != queue.end()) {
            prio = it.key();
            int count = queue.remove(it.key(),it.value());
            if (count>0)
                qDebug() << "cachingQueue()::del" << count << "entries from queue for" << funcString[func] << "on receiver" << receiver;
        }
    }
    return prio;
}


queuePriority cachingQueue::getQueued(funcs func, uchar receiver)
{
    queuePriority prio = priorityNone;
    if (func != funcNone)
    {
        QMutexLocker locker(&mutex);
        auto it = std::find_if(queue.begin(), queue.end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
        //auto it(queue.begin());
        if (it != queue.end()) {
            prio = it.key();
        }
    }
    return prio;
}

queuePriority cachingQueue::changePriority(queuePriority newprio, funcs func, uchar receiver)
{
    queuePriority prio = priorityNone;
    if (func != funcNone && newprio != priorityImmediate)
    {
        QMutexLocker locker(&mutex);
        auto it = std::find_if(queue.begin(), queue.end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
        //auto it(queue.begin());
        if (it != queue.end()) {
            prio = it.key();
            auto item = it.value();
            it=queue.erase(it);
            queue.insert(newprio,item);
        }
    }
    return prio;
}

queuePriority cachingQueue::isRecurring(funcs func, uchar receiver)
{
    // Does NOT lock the mutex
    auto rec = std::find_if(queue.begin(), queue.end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
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

    if (mutex.tryLock(CACHE_LOCK_TIME)) {
        messages.append(msg);
        mutex.unlock();
        qDebug() << "Received:" << msg;
#ifndef Q_OS_MACOS
        waiting.wakeOne();
#endif
    } else {
        qWarning(logRig()) << "Queue failed to send message() after" << CACHE_LOCK_TIME << "ms, mutex locked";
    }
}

void cachingQueue::receiveValue(funcs func, QVariant value, uchar receiver)
{
    if (mutex.tryLock(CACHE_LOCK_TIME)) {
        cacheItem c = cacheItem(func,value,receiver);
        items.enqueue(c);
        updateCache(true,func,value,receiver);        
        mutex.unlock();
#ifndef Q_OS_MACOS
        waiting.wakeOne();
#endif
    } else {
        qWarning(logRig()) << "Failed to receiveValue() after" << CACHE_LOCK_TIME << "ms, mutex locked";
    }
}


void cachingQueue::updateCache(bool reply, queueItem item)
{
    // Mutex MUST be locked by the calling function.

    // We need to make sure that all "main" frequencies/modes are updated.
    if (reply)
    {
        if (item.command == funcFreqTR)
        {
            if (rigCaps->commands.contains(funcFreq))
                item.command = funcFreq;
            else if (rigCaps->commands.contains(funcSelectedFreq))
                item.command = funcSelectedFreq;
            else
                item.command = funcFreqGet;
        }
        else if (item.command == funcModeTR)
        {
            if (rigCaps->commands.contains(funcMode))
                item.command = funcMode;
            else if (rigCaps->commands.contains(funcSelectedMode))
                item.command = funcSelectedMode;
            else
                item.command = funcModeGet;
        }

        // Is this a command that might have updated our state?
        if (item.command == funcSatelliteMode && item.param.value<bool>())
            rigState.vfoMode=vfoModeType_t::vfoModeSat;
        if (item.command == funcMemoryMode && item.param.value<bool>())
            rigState.vfoMode=vfoModeType_t::vfoModeMem;
        if (item.command == funcVFOMode && item.param.value<bool>())
            rigState.vfoMode=vfoModeType_t::vfoModeVfo;
        if (item.command == funcScopeMainSub)
            rigState.receiver = item.param.value<uchar>();
    } else {
        // If we are requesting a particular VFO, set our state as the rig will not reply
        if (item.command == funcSelectVFO) {
            rigState.vfo = item.param.value<vfo_t>();
        } else if (item.command == funcVFOASelect) {
            rigState.vfo = vfo_t::vfoA;
        } else if (item.command == funcVFOBSelect && rigCaps->numVFO > 1) {
            rigState.vfo = vfo_t::vfoB;
        } else if (item.command == funcVFOMainSelect) {
            rigState.vfo = vfo_t::vfoMain;
        } else if (item.command == funcVFOSubSelect && rigCaps->numReceiver > 1) {
            rigState.vfo = vfo_t::vfoSub;
            rigState.receiver=1;
        }
    }

    auto cv = cache.find(item.command);
    while (cv != cache.end() && cv->command == item.command) {
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
    cache.insert(item.command,c);

}


void cachingQueue::updateCache(bool reply, funcs func, QVariant value, uchar receiver)
{
    queueItem q(func,value,false,receiver);
    updateCache(reply,q);
}


cacheItem cachingQueue::getCache(funcs func, uchar receiver)
{
    cacheItem ret;
    if (func != funcNone) {
        QMutexLocker locker(&mutex);
        auto it = cache.find(func);
        while (it != cache.end() && it->command == func)
        {
            if (it->receiver == receiver)
                ret = cacheItem(*it);
            ++it;
        }
    }
    // If the cache is more than 5-20 seconds old, re-request it as it may be stale (maybe make this a config option?)
    // Using priorityhighest WILL slow down the S-Meter when a command intensive client is connected to rigctl
    if (func != funcNone && func != funcPowerControl && (!ret.value.isValid() || ret.reply.addSecs(QRandomGenerator::global()->bounded(5,20)) <= QDateTime::currentDateTime())) {
        //qInfo() << "No (or expired) cache found for" << funcString[func] << "requesting";
        add(priorityHighest,func,false,receiver);
    }
    return ret;
}

//Calling function MUST call unlockMutex() once finished with data
QMultiMap<funcs,cacheItem>* cachingQueue::getCacheItems()
{
    mutex.lock();
    return &cache;
}

//Calling function MUST call unlockMutex() once finished with data
QMultiMap <queuePriority,queueItem>* cachingQueue::getQueueItems()
{
    mutex.lock();
    return &queue;
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
        } else if (!strcmp(a.typeName(),"meter_t")) {
            if (a.value<meter_t>() != b.value<meter_t>())
                changed=true;
        } else if (!strcmp(a.typeName(),"vfo_t")) {
            if (a.value<vfo_t>() != b.value<vfo_t>())
                changed=true;
        } else if (!strcmp(a.typeName(),"lpfhpf")) {
            if (a.value<lpfhpf>().lpf != b.value<lpfhpf>().lpf || a.value<lpfhpf>().hpf != b.value<lpfhpf>().hpf  )
                changed=true;
        } else if (!strcmp(a.typeName(),"spectrumBounds")) {
            if (a.value<spectrumBounds>().edge != b.value<spectrumBounds>().edge ||
                    a.value<spectrumBounds>().start != b.value<spectrumBounds>().start ||
                    a.value<spectrumBounds>().end != b.value<spectrumBounds>().end  )
                changed=true;
        } else if (!strcmp(a.typeName(),"centerSpanData")) {
            if (a.value<centerSpanData>().cstype != b.value<centerSpanData>().cstype || a.value<centerSpanData>().freq != b.value<centerSpanData>().freq  )
                changed=true;
        } else if (!strcmp(a.typeName(),"rptrAccessData")) {
            if (a.value<rptrAccessData>().accessMode != b.value<rptrAccessData>().accessMode ||
                            a.value<rptrAccessData>().turnOffTSQL != b.value<rptrAccessData>().turnOffTSQL ||
                            a.value<rptrAccessData>().turnOffTone != b.value<rptrAccessData>().turnOffTone)
                changed=true;
        } else if (!strcmp(a.typeName(),"scopeData") || !strcmp(a.typeName(),"memoryType")
                   || !strcmp(a.typeName(),"bandStackType")  || !strcmp(a.typeName(),"timekind") || !strcmp(a.typeName(),"datekind")
                   || !strcmp(a.typeName(),"meterkind") || !strcmp(a.typeName(),"udpPreferences")) {
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

