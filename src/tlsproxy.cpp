#include "tlsproxy.h"

#ifdef Q_OS_MACOS

#include <QDebug>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <chrono>
#include <thread>
#include <vector>

TlsProxyWorker::TlsProxyWorker(quint16 listenPort, quint16 backendPort,
                               const QString &certPath, const QString &keyPath,
                               QObject *parent)
    : QObject(parent)
    , m_listenPort(listenPort)
    , m_backendPort(backendPort)
    , m_certPath(certPath)
    , m_keyPath(keyPath)
{
}

TlsProxyWorker::~TlsProxyWorker()
{
    stop();
}

void TlsProxyWorker::start()
{
    // Create SSL context
    m_ctx = SSL_CTX_new(TLS_server_method());
    if (!m_ctx) {
        emit error("Failed to create SSL_CTX");
        return;
    }

    // Do NOT request client certificates — this is the whole point
    SSL_CTX_set_verify(m_ctx, SSL_VERIFY_NONE, nullptr);

    if (SSL_CTX_use_certificate_file(m_ctx, m_certPath.toLocal8Bit().constData(), SSL_FILETYPE_PEM) != 1) {
        emit error("Failed to load certificate: " + m_certPath);
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        return;
    }
    if (SSL_CTX_use_PrivateKey_file(m_ctx, m_keyPath.toLocal8Bit().constData(), SSL_FILETYPE_PEM) != 1) {
        emit error("Failed to load private key: " + m_keyPath);
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        return;
    }

    // Create listening socket
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        emit error("Failed to create socket");
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        return;
    }

    int optval = 1;
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_listenPort);

    if (bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        emit error(QString("Failed to bind to port %1").arg(m_listenPort));
        close(m_listenFd); m_listenFd = -1;
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        return;
    }

    if (listen(m_listenFd, 16) < 0) {
        emit error("Failed to listen");
        close(m_listenFd); m_listenFd = -1;
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        return;
    }

    m_running = true;
    qInfo() << "Web: OpenSSL TLS proxy listening on port" << m_listenPort
            << "-> localhost:" << m_backendPort;
    emit listening();

    acceptLoop();
}

void TlsProxyWorker::stop()
{
    m_running = false;
    if (m_listenFd >= 0) {
        ::shutdown(m_listenFd, SHUT_RDWR);
        close(m_listenFd);
        m_listenFd = -1;
    }
    if (m_ctx) {
        SSL_CTX_free(m_ctx);
        m_ctx = nullptr;
    }
}

void TlsProxyWorker::acceptLoop()
{
    while (m_running) {
        struct pollfd pfd{};
        pfd.fd = m_listenFd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 500);
        if (ret <= 0) continue;
        if (!m_running) break;

        struct sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(m_listenFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) continue;

        // Spawn a detached thread per connection
        SSL_CTX *ctx = m_ctx;
        quint16 backendPort = m_backendPort;
        std::thread([clientFd, ctx, backendPort]() {
            TlsProxyWorker::proxyConnection(clientFd, ctx, backendPort);
        }).detach();
    }
}

// One connection, relayed until either side ends it.
//
// Both sockets are non-blocking and every wait is one poll() over the two
// of them.  The first version read the client with a blocking SSL_read()
// whenever poll() said its socket was readable: a TLS record that carried no
// application data, or half of one, parked the thread inside SSL_read() while
// the backend's answer sat unread, and decrypted bytes OpenSSL already held
// were only looked for through SSL_pending().  A WSS upgrade went unanswered
// for 10 s on the macOS CI runner now and then (2026-10-27, 2026-11-15) and
// was never reproduced elsewhere, so the cause is not proven; what is certain
// is that nothing here can block on one direction any more, and that the
// log now says which side was silent.
namespace {

bool setNonBlocking(int fd)
{
    const int fl = fcntl(fd, F_GETFL, 0);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}

// A peer that went away must end the relay, not the process.
void noSigPipe(int fd)
{
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    Q_UNUSED(fd);
#endif
}

#ifdef MSG_NOSIGNAL
const int kSendFlags = MSG_NOSIGNAL;
#else
const int kSendFlags = 0;
#endif

qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

const int kHandshakeTimeoutMs = 10000;   // a client that never finishes TLS
const int kBackendSilentMs = 3000;       // a request the web server has not answered: say so

} // namespace

void TlsProxyWorker::proxyConnection(int clientFd, SSL_CTX *ctx, quint16 backendPort)
{
    noSigPipe(clientFd);
    SSL *ssl = SSL_new(ctx);
    if (!ssl || !setNonBlocking(clientFd)) {
        if (ssl) SSL_free(ssl);
        close(clientFd);
        return;
    }
    SSL_set_fd(ssl, clientFd);
    // A write may be retried from a buffer position that moved on.
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    // TLS handshake, bounded.
    const qint64 t0 = nowMs();
    for (;;) {
        const int r = SSL_accept(ssl);
        if (r == 1) break;
        const int err = SSL_get_error(ssl, r);
        const qint64 left = kHandshakeTimeoutMs - (nowMs() - t0);
        if ((err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) || left <= 0) {
            if (left <= 0)
                qWarning() << "Web: TLS proxy: handshake not finished in" << kHandshakeTimeoutMs << "ms, dropped";
            SSL_free(ssl);
            close(clientFd);
            return;
        }
        struct pollfd pfd{};
        pfd.fd = clientFd;
        pfd.events = (err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT;
        if (poll(&pfd, 1, int(left)) < 0 && errno != EINTR) {
            SSL_free(ssl);
            close(clientFd);
            return;
        }
    }
    const qint64 tlsMs = nowMs() - t0;

    // Connect to the internal plain HTTP server (loopback: immediate).
    int backendFd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in backendAddr{};
    backendAddr.sin_family = AF_INET;
    backendAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    backendAddr.sin_port = htons(backendPort);
    if (backendFd < 0
        || ::connect(backendFd, reinterpret_cast<struct sockaddr*>(&backendAddr), sizeof(backendAddr)) < 0
        || !setNonBlocking(backendFd)) {
        qWarning() << "Web: TLS proxy: no connection to the internal server on port" << backendPort;
        if (backendFd >= 0) close(backendFd);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(clientFd);
        return;
    }
    noSigPipe(backendFd);

    // One buffer per direction; a side is read only while its buffer is empty.
    static const int kBuf = 16384;
    std::vector<char> c2b(kBuf), b2c(kBuf);
    int c2bLen = 0, c2bOff = 0;      // decrypted, waiting for the backend
    int b2cLen = 0, b2cOff = 0;      // from the backend, waiting for SSL_write
    bool readWantsWrite = false;     // SSL_read needs the socket writable
    bool writeWantsRead = false;     // SSL_write needs the socket readable
    qint64 toBackend = 0, fromBackend = 0;
    qint64 firstRequestMs = 0;       // when the first bytes went to the backend
    bool silentLogged = false;

    for (;;) {
        bool progress = false;

        // TLS client -> buffer.  Tried on every pass: OpenSSL may hold bytes
        // the socket no longer shows as readable.
        if (c2bLen == 0) {
            const int n = SSL_read(ssl, c2b.data(), kBuf);
            if (n > 0) {
                c2bLen = n; c2bOff = 0; progress = true;
                readWantsWrite = false;
            } else {
                const int err = SSL_get_error(ssl, n);
                if (err == SSL_ERROR_WANT_READ) readWantsWrite = false;
                else if (err == SSL_ERROR_WANT_WRITE) readWantsWrite = true;
                else break;                                  // closed or failed
            }
        }

        // buffer -> backend
        if (c2bLen > 0) {
            const ssize_t w = send(backendFd, c2b.data() + c2bOff, size_t(c2bLen), kSendFlags);
            if (w > 0) {
                if (toBackend == 0) firstRequestMs = nowMs();
                toBackend += w; c2bOff += int(w); c2bLen -= int(w); progress = true;
            } else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                break;
            }
        }

        // backend -> buffer
        if (b2cLen == 0) {
            const ssize_t n = recv(backendFd, b2c.data(), size_t(kBuf), 0);
            if (n > 0) {
                b2cLen = int(n); b2cOff = 0; fromBackend += n; progress = true;
            } else if (n == 0) {
                break;                                       // closed, and nothing left to pass on
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                break;
            }
        }

        // buffer -> TLS client
        if (b2cLen > 0) {
            const int w = SSL_write(ssl, b2c.data() + b2cOff, b2cLen);
            if (w > 0) {
                b2cOff += w; b2cLen -= w; progress = true;
                writeWantsRead = false;
            } else {
                const int err = SSL_get_error(ssl, w);
                if (err == SSL_ERROR_WANT_WRITE) writeWantsRead = false;
                else if (err == SSL_ERROR_WANT_READ) writeWantsRead = true;
                else break;
            }
        }

        if (progress) continue;

        struct pollfd fds[2];
        fds[0].fd = clientFd;
        fds[0].events = 0;
        fds[0].revents = 0;
        if ((c2bLen == 0 && !readWantsWrite) || (b2cLen > 0 && writeWantsRead)) fds[0].events |= POLLIN;
        if ((b2cLen > 0 && !writeWantsRead) || (c2bLen == 0 && readWantsWrite)) fds[0].events |= POLLOUT;
        fds[1].fd = backendFd;
        fds[1].events = 0;
        fds[1].revents = 0;
        if (b2cLen == 0) fds[1].events |= POLLIN;
        if (c2bLen > 0) fds[1].events |= POLLOUT;

        // While a request is unanswered, wake up to say so once.
        const bool waiting = toBackend > 0 && fromBackend == 0 && !silentLogged;
        const int ret = poll(fds, 2, waiting ? 500 : 30000);
        if (ret < 0 && errno != EINTR) break;
        if (waiting && nowMs() - firstRequestMs > kBackendSilentMs) {
            silentLogged = true;
            qWarning() << "Web: TLS proxy: the internal server has not answered" << toBackend
                       << "request bytes for" << (nowMs() - firstRequestMs) << "ms (TLS handshake took"
                       << tlsMs << "ms)";
        }
    }

    if (silentLogged)
        qWarning() << "Web: TLS proxy: that connection ended after" << (nowMs() - firstRequestMs)
                   << "ms with" << fromBackend << "bytes from the internal server";

    close(backendFd);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(clientFd);
}

#endif // Q_OS_MACOS
