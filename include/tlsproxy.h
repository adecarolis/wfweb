#ifndef TLSPROXY_H
#define TLSPROXY_H

#include <QtGlobal>

#ifdef Q_OS_MACOS

#include <QObject>
#include <QThread>
#include <QHostAddress>
#include <atomic>
#include <vector>

// Forward declarations — avoid exposing OpenSSL headers
typedef struct ssl_ctx_st SSL_CTX;

class TlsProxyWorker : public QObject {
    Q_OBJECT
public:
    TlsProxyWorker(quint16 listenPort, quint16 backendPort,
                   const QString &certPath, const QString &keyPath,
                   QObject *parent = nullptr);
    ~TlsProxyWorker();

    // The browser behind a connection the internal server sees coming from
    // this proxy: every one of them arrives from 127.0.0.1, told apart by
    // the proxy's local port.  Without this every client of a macOS server
    // had the same address: one lockout for all of them, an audit log with
    // nothing in it, and --trust-proxy 127.0.0.1 trusting the whole world.
    // Thread-safe.  False when the port is not one of ours.
    static bool clientOf(quint16 proxyPort, QHostAddress *client);

public slots:
    void start();
    void stop();

signals:
    void listening();
    void error(QString message);

private:
    void acceptLoop();
    static void proxyConnection(int clientFd, SSL_CTX *ctx, quint16 backendPort, quint32 clientIp);

    quint16 m_listenPort;
    quint16 m_backendPort;
    QString m_certPath;
    QString m_keyPath;
    SSL_CTX *m_ctx = nullptr;
    int m_listenFd = -1;
    std::atomic<bool> m_running{false};
};

#endif // Q_OS_MACOS
#endif // TLSPROXY_H
