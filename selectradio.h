#ifndef SELECTRADIO_H
#define SELECTRADIO_H

#include <QDialog>
#include <QList>
#include <QtEndian>
#include <QHostInfo>
#include "packettypes.h"

namespace Ui {
    class selectRadio;
}

class selectRadio : public QDialog
{
    Q_OBJECT

public:
    explicit selectRadio(QWidget* parent = 0);
    ~selectRadio();
    void populate(QList<radio_cap_packet> radios);

public slots:
    void on_table_cellClicked(int row, int col);
    void setInUse(int radio, bool busy, QString user, QString ip);
    void on_cancelButton_clicked();

signals:
    void selectedRadio(int radio);

private:
    Ui::selectRadio* ui;

};

#endif // SELECTRADIO_H
