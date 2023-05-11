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
QMutex cachingQueue::mutex;

cachingQueue *cachingQueue::getInstance(QObject* parent)
{
    QMutexLocker locker = QMutexLocker(&mutex);
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
    int counter=0;
    while (!aborted)
    {
        if (!waiting.wait(&mutex, deadline.remainingTime()))
        {
            // Time to process the queue - mutex is locked
            //qInfo("Deadline!");
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
                queueItem item = it.value();

                emit haveCommand(item.type, item.command,item.param);

                it=queue.erase(it);
                while (it.key()==prio)
                    it++;
                // Add it back into the queue
                if (item.recurring)
                    queue.insert(it,prio,item);
            }

            QCoreApplication::processEvents();
            deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency

        }
        else if (!aborted) {
            // We have been woken by a command that needs immediate attention (mutex is locked)
            qInfo("Message Received");
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
    QMutexLocker locker(&mutex);
    queue.insert(prio, queueItem(func,recurring));
    if (recurring) qInfo() << "adding" << funcString[func] << "recurring" << recurring << "priority" << prio;
}

void cachingQueue::add(queuePriority prio ,queueItem item)
{
    QMutexLocker locker(&mutex);
    queue.insert(prio, item);
    if (item.recurring) qInfo() << "adding" << funcString[item.command] << "recurring" << item.recurring << "priority" << prio;
}

void cachingQueue::addUnique(queuePriority prio ,queueItem item)
{
    QMutexLocker locker(&mutex);
    queue.erase(std::remove_if(queue.begin(), queue.end(), [item](const queueItem& c) {  return (c.command == item.command); }), queue.end());
    queue.insert(prio, item);
    if (item.recurring) qInfo() << "adding" << funcString[item.command] << "recurring" << item.recurring << "priority" << prio;
}

void cachingQueue::del(funcs func)
{
    QMutexLocker locker(&mutex);
    queue.erase(std::remove_if(queue.begin(), queue.end(), [func](const queueItem& c) {  return (c.command == func); }), queue.end());
    qInfo() << "deleting" << funcString[func];
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


void cachingQueue::setCache(funcs func, QVariant val)
{
    QMutexLocker locker = QMutexLocker(&mutex);
    cache.insert(func,val);
}


QVariant cachingQueue::getCache(funcs func)
{
    QMutexLocker locker = QMutexLocker(&mutex);
    auto it = cache.find(func);
    if (it != cache.end())
    {
        return it.value();
    }
    return NULL;
}

bool cachingQueue::getCache(funcs func, quint8& val)
{
    QVariant variant = getCache(func);
    if (variant.isNull())
        return false;
    val=variant.toUInt();
    return true;
}

bool cachingQueue::getCache(funcs func, quint16& val)
{
    QVariant variant = getCache(func);
    if (variant.isNull())
        return false;
    val=variant.toUInt();
    return true;
}

bool cachingQueue::getCache(funcs func, quint32& val)
{
    QVariant variant = getCache(func);
    if (variant.isNull())
        return false;
    val=variant.toUInt();
    return true;
}

