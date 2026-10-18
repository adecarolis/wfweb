#include "logtail.h"

LogTail::LogTail(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &LogTail::poll);
}

void LogTail::follow(const QString &path)
{
    stop();
    m_file.setFileName(path);
    m_position = 0;
    m_partial.clear();
    m_timer.start();
}

void LogTail::stop()
{
    const bool wasFollowing = m_timer.isActive();
    m_timer.stop();
    if (wasFollowing)
        poll();
    if (m_file.isOpen())
        m_file.close();
    if (!m_partial.isEmpty()) {
        emit line(QString::fromLocal8Bit(m_partial));
        m_partial.clear();
    }
}

void LogTail::poll()
{
    if (!m_file.isOpen()) {
        // Unbuffered: every read goes to the OS, so a file another process
        // keeps appending to is seen as it grows.
        if (!m_file.exists() || !m_file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
            return;
    }

    const qint64 size = m_file.size();
    if (size < m_position) {
        // Truncated: the server opened it afresh.
        m_position = 0;
        m_partial.clear();
    }
    if (size == m_position || !m_file.seek(m_position))
        return;

    const QByteArray chunk = m_file.read(size - m_position);
    m_position += chunk.size();
    m_partial += chunk;

    int newline;
    while ((newline = m_partial.indexOf('\n')) >= 0) {
        QByteArray one = m_partial.left(newline);
        m_partial.remove(0, newline + 1);
        if (one.endsWith('\r'))
            one.chop(1);
        // The server writes the file through QTextStream in the locale codec.
        emit line(QString::fromLocal8Bit(one));
    }
}
