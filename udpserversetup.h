#ifndef UDPSERVERSETUP_H
#define UDPSERVERSETUP_H

#include <QDialog>
#include <QComboBox>
#include <QList>

#include <QDebug>


struct SERVERUSER {
    QString username;
    QString password;
    quint8 userType;
};

struct SERVERCONFIG {
    bool enabled;
    bool lan;
    quint16 controlPort;
    quint16 civPort;
    quint16 audioPort;
    int audioOutput;
    int audioInput;
    quint8 resampleQuality;
    quint32 baudRate;

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

private slots:
    void on_usersTable_cellClicked(int row, int col);
    void onPasswordChanged();

public slots:    
    void receiveServerConfig(SERVERCONFIG conf);

signals:
    void serverConfig(SERVERCONFIG conf, bool store);

private:
    Ui::udpServerSetup* ui;
    void accept();
    QList<QComboBox*> userTypes;
    void addUserLine(const QString &user, const QString &pass, const int &type);
};

#endif // UDPSERVER_H
