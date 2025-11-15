#ifndef BYTERING_H
#define BYTERING_H

// =============================================================
// Shared ring buffer header (used by RtAudio/PortAudio)
// =============================================================


#include <QByteArray>
#include <QMutex>
#include <cstring>


class ByteRing {
public:
    explicit ByteRing(size_t cap = 1u<<18) : cap_(cap) {}
    size_t push(const char* p, size_t n) {
        QMutexLocker l(&m_);
        const size_t free = cap_ - static_cast<size_t>(buf_.size());
        const size_t take = qMin(n, free);
        buf_.append(p, static_cast<int>(take));
        return take;
    }
    size_t pop(char* p, size_t n) {
        QMutexLocker l(&m_);
        const size_t take = qMin(n, static_cast<size_t>(buf_.size()));
        if (take) {
            std::memcpy(p, buf_.constData(), take);
            buf_.remove(0, static_cast<int>(take));
        }
        return take;
    }
    size_t size() const { QMutexLocker l(&m_); return static_cast<size_t>(buf_.size()); }
    size_t capacity() const { return cap_; }
private:
    size_t cap_;
    mutable QMutex m_;
    QByteArray buf_;
};

#endif // BYTERING_H
