#ifndef EXTRIGCTLD_H
#define EXTRIGCTLD_H

#include <QObject>
#include <QHash>
#include <QByteArray>

class ExtSlot;
class QTcpServer;
class QTcpSocket;

// Tiny Hamlib NET rigctl-compatible TCP server. Parses one command per
// line, dispatches against the owning ExtSlot, and replies in either bare
// or extended format depending on whether the command was prefixed with
// `\` (extended). Supports the subset JS8Call (and most amateur apps)
// actually issues:
//   f F m M t T v V s S i I x X
//   dump_state chk_vfo get_powerstat set_powerstat
//   q (quit)
class ExtRigctld : public QObject
{
    Q_OBJECT
public:
    explicit ExtRigctld(ExtSlot* slot, QObject* parent = nullptr);
    ~ExtRigctld() override;

    bool listen(quint16 port);
    void stop();

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleLine(QTcpSocket* s, const QByteArray& line);
    void writeData(QTcpSocket* s, const QByteArray& data);

    // Each handler returns the body to send; `extended` controls whether
    // a final RPRT footer is added (extended mode) or whether get-style
    // commands also include "Key: value" prefixes per response line.
    // For our minimal subset we accept both forms but format identically
    // — the bare protocol is what NetRigctl reads by default.

    ExtSlot* slot;
    QTcpServer* server = nullptr;
    QHash<QTcpSocket*, QByteArray> buffers;
};

#endif // EXTRIGCTLD_H
