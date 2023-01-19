#ifndef TRANSCEIVERADJUSTMENTS_H
#define TRANSCEIVERADJUSTMENTS_H



#include <QtGlobal>

#include <QWidget>
#include "rigidentities.h"


namespace Ui {
class transceiverAdjustments;
}

class transceiverAdjustments : public QWidget
{
    Q_OBJECT

public:
    explicit transceiverAdjustments(QWidget *parent = 0);
    ~transceiverAdjustments();

signals:
    void setIFShift(unsigned char level);
    void setTPBFInner(unsigned char level);
    void setTPBFOuter(unsigned char level);

public slots:
    void setRig(rigCapabilities rig);
    void updateIFShift(unsigned char level);
    void updateTPBFInner(unsigned char level);
    void updateTPBFOuter(unsigned char level);

private slots:

    void on_IFShiftSlider_valueChanged(int value);

    void on_TPBFInnerSlider_valueChanged(int value);

    void on_TPBFOuterSlider_valueChanged(int value);

    void on_resetPBTbtn_clicked();

private:
    Ui::transceiverAdjustments *ui;
    rigCapabilities rigCaps;
    bool haveRigCaps = false;
    int previousIFShift = 128;
};

#endif // TRANSCEIVERADJUSTMENTS_H
