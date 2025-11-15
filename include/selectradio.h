#ifndef SELECTRADIO_H
#define SELECTRADIO_H

#include <QDialog>
#include <QList>
#include <QtEndian>
#include <QHostInfo>

#include <qcustomplot.h>

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
    void audioOutputLevel(quint16 level);
    void audioInputLevel(quint16 level);
    void addTimeDifference(qint64 time);

public slots:
    void on_table_cellClicked(int row, int col);
    void setInUse(quint8 radio, bool admin, quint8 busy, QString user, QString ip);
    void on_cancelButton_clicked();
    void spectrumTime(double time);
    void waterfallTime(double time);


signals:
    void selectedRadio(quint8 radio);

private:
    Ui::selectRadio* ui;
};

#endif // SELECTRADIO_H
