#ifndef FREEDVREPORTER_H
#define FREEDVREPORTER_H

#include "spotreporter.h"
#include <QWebSocket>
#include <QTimer>
#include <QJsonObject>

class FreeDVReporter : public SpotReporter
{
    Q_OBJECT
public:
    explicit FreeDVReporter(QObject *parent = nullptr);
    ~FreeDVReporter() override;

    void setStation(const QString &callsign, const QString &grid) override;
    void connectToService() override;
    void disconnectFromService() override;
    void updateFrequency(quint64 hz) override;
    void updateTx(const QString &mode, bool transmitting) override;
    void updateRxSpot(const QString &callsign, const QString &mode, int snr) override;
    void setMessage(const QString &message);

    State state() const override { return state_; }

private slots:
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessage(const QString &message);
    void onWsError(QAbstractSocket::SocketError error);
    void onPingTimeout();
    void onReconnectTimer();
    void onFreqDebounce();

private:
    void setState(State s);
    void sioEmit(const QString &event, const QJsonObject &data);
    void sioEmitNoPayload(const QString &event);
    void resendState();
    void scheduleReconnect();
    static QString reporterModeName(const QString &mode);

    static constexpr int PROTOCOL_VERSION = 2;
    static constexpr const char *DEFAULT_HOST = "qso.freedv.org";
    static constexpr int DEFAULT_PORT = 80;

    QWebSocket *socket_ = nullptr;
    QTimer *pingTimer_ = nullptr;
    QTimer *reconnectTimer_ = nullptr;
    QTimer *freqDebounceTimer_ = nullptr;

    State state_ = Disconnected;
    bool intentionalDisconnect_ = false;
    int reconnectDelay_ = 2000;
    int pingTimeout_ = 20000;

    // Station config
    QString callsign_;
    QString grid_;

    // Cached state for resend after reconnect
    quint64 lastFreq_ = 0;
    quint64 pendingFreq_ = 0;
    QString lastMode_;
    bool lastTx_ = false;
    QString lastMessage_;
    bool fullyConnected_ = false;
};

#endif // FREEDVREPORTER_H
