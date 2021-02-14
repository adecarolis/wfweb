#ifndef UDPSERVERSETUP_H
#define UDPSERVERSETUP_H

#include <QDialog>

namespace Ui {
    class udpServerSetup;
}

class udpServerSetup : public QDialog
{
    Q_OBJECT

public:
    explicit udpServerSetup(QWidget* parent = 0);
    ~udpServerSetup();

private:
    Ui::udpServerSetup* ui;
};

#endif // UDPSERVER_H
