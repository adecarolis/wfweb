#include "transceiveradjustments.h"
#include "ui_transceiveradjustments.h"

transceiverAdjustments::transceiverAdjustments(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::transceiverAdjustments)
{
    ui->setupUi(this);
    // request level updates

}

transceiverAdjustments::~transceiverAdjustments()
{
    rigCaps.inputs.clear();
    rigCaps.preamps.clear();
    rigCaps.attenuators.clear();
    rigCaps.antennas.clear();

    delete ui;
}

void transceiverAdjustments::on_IFShiftSlider_valueChanged(int value)
{
    if(rigCaps.hasIFShift)
    {
        emit setIFShift(value);
    } else {
        unsigned char inner = ui->TPBFInnerSlider->value();
        unsigned char outer = ui->TPBFOuterSlider->value();
        int shift = value - previousIFShift;
        inner = MAX( 0, MIN(255,int (inner + shift)) );
        outer = MAX( 0, MIN(255,int (outer + shift)) );

        ui->TPBFInnerSlider->setValue(inner);
        ui->TPBFOuterSlider->setValue(outer);
        previousIFShift = value;
    }
}

void transceiverAdjustments::on_TPBFInnerSlider_valueChanged(int value)
{
    emit setTPBFInner(value);
}

void transceiverAdjustments::on_TPBFOuterSlider_valueChanged(int value)
{
    emit setTPBFOuter(value);
}

void transceiverAdjustments::setRig(rigCapabilities rig)
{
    this->rigCaps = rig;
    if(!rigCaps.hasIFShift)
        updateIFShift(128);
    //ui->IFShiftSlider->setVisible(rigCaps.hasIFShift);
    //ui->IFShiftLabel->setVisible(rigCaps.hasIFShift);

    ui->TPBFInnerSlider->setVisible(rigCaps.hasTBPF);
    ui->TPBFInnerLabel->setVisible(rigCaps.hasTBPF);

    ui->TPBFOuterSlider->setVisible(rigCaps.hasTBPF);
    ui->TPBFInnerLabel->setVisible(rigCaps.hasTBPF);

    haveRigCaps = true;
}

// These are accessed by wfmain when we receive new values from rigCommander:
void transceiverAdjustments::updateIFShift(unsigned char level)
{
    ui->IFShiftSlider->blockSignals(true);
    ui->IFShiftSlider->setValue(level);
    ui->IFShiftSlider->blockSignals(false);
}

void transceiverAdjustments::updateTPBFInner(unsigned char level)
{
    ui->TPBFInnerSlider->blockSignals(true);
    ui->TPBFInnerSlider->setValue(level);
    ui->TPBFInnerSlider->blockSignals(false);
}

void transceiverAdjustments::updateTPBFOuter(unsigned char level)
{
    ui->TPBFOuterSlider->blockSignals(true);
    ui->TPBFOuterSlider->setValue(level);
    ui->TPBFOuterSlider->blockSignals(false);
}
