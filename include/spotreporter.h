#ifndef SPOTREPORTER_H
#define SPOTREPORTER_H

#include <QObject>
#include <QString>

class SpotReporter : public QObject
{
    Q_OBJECT
public:
    enum State { Disconnected, Connecting, Connected, Error };
    Q_ENUM(State)

    using QObject::QObject;
    virtual ~SpotReporter() = default;

    virtual void setStation(const QString &callsign, const QString &grid) = 0;
    virtual void connectToService() = 0;
    virtual void disconnectFromService() = 0;
    virtual void updateFrequency(quint64 hz) = 0;
    virtual void updateTx(const QString &mode, bool transmitting) = 0;
    virtual void updateRxSpot(const QString &callsign, const QString &mode, int snr) = 0;

    virtual State state() const = 0;

signals:
    void stateChanged(SpotReporter::State state);
};

#endif // SPOTREPORTER_H
