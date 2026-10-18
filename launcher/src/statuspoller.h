#ifndef STATUSPOLLER_H
#define STATUSPOLLER_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QNetworkReply;

// Readiness and rig status from the server's REST API. A bind failure does
// not make the server exit and its stdout is buffered, so the only reliable
// "it is up" signal is an answer to GET /api/v1/radio/info.
//
// begin() is given the plain-HTTP candidates in order of likelihood: the
// REST port next to HTTPS first (webPort+1), then the web port itself, where
// REST lives when the server fell back to plain HTTP. They are tried in turn
// every 500 ms; the first JSON answer with a "version" field fixes the URL
// and the scheme (the first candidate answering means HTTPS on the web
// port), after which the same URL is polled every 2 s for the rig state.
class StatusPoller : public QObject
{
    Q_OBJECT
public:
    explicit StatusPoller(QObject *parent = nullptr);

    void begin(const QStringList &probeUrls);
    void stop();

    bool isListening() const { return m_listening; }
    bool https() const { return m_https; }

    static const int probeIntervalMs = 500;
    static const int pollIntervalMs = 2000;
    static const int deadlineMs = 20000;

signals:
    void listening(bool https);
    void status(const QJsonObject &info);
    void timedOut();

private:
    void tick();
    void onReply(QNetworkReply *reply, const QString &url);

    QNetworkAccessManager m_nam;
    QTimer m_timer;
    QElapsedTimer m_since;
    QStringList m_urls;
    int m_next = 0;
    QString m_url;
    bool m_listening = false;
    bool m_https = false;
    QNetworkReply *m_inflight = nullptr;
};

#endif // STATUSPOLLER_H
