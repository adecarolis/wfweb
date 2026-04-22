#ifndef CONTROLSERVER_H
#define CONTROLSERVER_H

#include <QByteArray>
#include <QHash>
#include <QObject>

class QTcpServer;
class QTcpSocket;
class channelMixer;

// Minimal HTTP/1.1 server bolted onto the virtualrig process so the bench
// can be driven live from a browser: view each rig's freq/mode/PTT, edit
// per-link per-band attenuation, and tweak per-rig noise floor without
// restarting. Serves one static HTML page and a tiny JSON API on the
// same port.
class controlServer : public QObject
{
    Q_OBJECT

public:
    explicit controlServer(channelMixer* mixer, QObject* parent = nullptr);
    ~controlServer();

    bool listen(quint16 port);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleRequest(QTcpSocket* s, const QByteArray& method,
                       const QByteArray& path, const QByteArray& body);
    void sendResponse(QTcpSocket* s, int status,
                      const QByteArray& contentType, const QByteArray& body);
    QByteArray renderStateJson() const;
    QByteArray applySetJson(const QByteArray& body);

    channelMixer* mixer;
    QTcpServer* server = nullptr;
    QHash<QTcpSocket*, QByteArray> buffers;
};

#endif
