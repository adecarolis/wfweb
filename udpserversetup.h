#ifndef UDPSERVERSETUP_H
#define UDPSERVERSETUP_H

#include <QDialog>

#include <QDebug>


struct SERVERUSER {
    QString username;
    QString password;
    quint8 userType;
};

struct SERVERCONFIG {
    bool enabled;
    quint16 controlPort;
    quint16 civPort;
    quint16 audioPort;
    QList <SERVERUSER> users;
};

namespace Ui {
    class udpServerSetup;
}

class udpServerSetup : public QDialog
{
    Q_OBJECT

public:
    explicit udpServerSetup(QWidget* parent = 0);
    ~udpServerSetup();

public slots:    
    void receiveServerConfig(SERVERCONFIG conf);

signals:
    void serverConfig(SERVERCONFIG conf, bool store);

private:
    Ui::udpServerSetup* ui;
    void accept();
};

#endif // UDPSERVER_H
