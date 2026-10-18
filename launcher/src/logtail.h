#ifndef LOGTAIL_H
#define LOGTAIL_H

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTimer>

// Follows the log file wfweb writes for -l. The server truncates it when it
// starts and flushes it after every line, so new bytes are read every
// 250 ms and handed out one complete line at a time. A file that shrinks
// (the server restarted it) is read again from the top; a file that does
// not exist yet is simply retried.
class LogTail : public QObject
{
    Q_OBJECT
public:
    explicit LogTail(QObject *parent = nullptr);

    void follow(const QString &path);
    // Reads what is left, then stops polling. Safe to call when idle.
    void stop();

    static const int intervalMs = 250;

signals:
    void line(const QString &text);

private:
    void poll();

    QFile m_file;
    qint64 m_position = 0;
    QByteArray m_partial;
    QTimer m_timer;
};

#endif // LOGTAIL_H
