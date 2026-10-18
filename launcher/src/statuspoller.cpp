#include "statuspoller.h"

#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

StatusPoller::StatusPoller(QObject *parent)
    : QObject(parent)
{
    // 127.0.0.1 must never go through a system proxy.
    m_nam.setProxy(QNetworkProxy::NoProxy);
    connect(&m_timer, &QTimer::timeout, this, &StatusPoller::tick);
}

void StatusPoller::begin(const QStringList &probeUrls)
{
    stop();
    m_urls = probeUrls;
    m_next = 0;
    m_url.clear();
    m_https = false;
    m_since.start();
    m_timer.start(probeIntervalMs);
    tick();
}

void StatusPoller::stop()
{
    m_timer.stop();
    m_listening = false;
    if (m_inflight) {
        QNetworkReply *reply = m_inflight;
        m_inflight = nullptr; // onReply ignores it from here on
        reply->abort();
        reply->deleteLater();
    }
}

void StatusPoller::tick()
{
    if (m_inflight || m_urls.isEmpty())
        return;
    if (!m_listening && m_since.elapsed() > deadlineMs) {
        m_timer.stop();
        emit timedOut();
        return;
    }
    const QString url = m_listening ? m_url : m_urls.at(m_next++ % m_urls.size());
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(1500);
    QNetworkReply *reply = m_nam.get(request);
    m_inflight = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() { onReply(reply, url); });
}

void StatusPoller::onReply(QNetworkReply *reply, const QString &url)
{
    reply->deleteLater();
    if (reply != m_inflight)
        return;
    m_inflight = nullptr;
    if (reply->error() != QNetworkReply::NoError)
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject() || !doc.object().contains(QStringLiteral("version")))
        return;

    if (!m_listening) {
        m_listening = true;
        m_url = url;
        m_https = (url == m_urls.first());
        m_timer.start(pollIntervalMs);
        emit listening(m_https);
    }
    emit status(doc.object());
}
