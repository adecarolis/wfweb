#include "transceiveradjustments.h"
#include "ui_transceiveradjustments.h"

transceiverAdjustments::transceiverAdjustments(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::transceiverAdjustments)
{
    ui->setupUi(this);

    // ---- These controls aren't finished yet:
    ui->transmitterControlsGroupBox->setVisible(false); // no controls available so far
    ui->bassRxLabel->setVisible(false);
    ui->bassRxSlider->setVisible(false);
    ui->trebleRxLabel->setVisible(false);
    ui->trebleRxSlider->setVisible(false);

    ui->NRRxCheckBox->setVisible(false);
    ui->NRRxSlider->setVisible(false);
    ui->notchRxChkBox->setVisible(false);
    ui->notchRxSlider->setVisible(false);
    ui->NBRxChkBox->setVisible(false);
    ui->NBRxSlider->setVisible(false);
    ui->bandwidthGroupBox->setVisible(false);
    ui->otherGrpBox->setVisible(false);
    // ----

    // Resize to fit new visible contents:
    this->window()->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    this->window()->resize(QSizePolicy::Minimum, QSizePolicy::Minimum);

    this->setWindowTitle("TransceiverAdjustments");
}

transceiverAdjustments::~transceiverAdjustments()
{
    rigCaps.inputs.clear();
    rigCaps.preamps.clear();
    rigCaps.attenuators.clear();
    rigCaps.antennas.clear();

    delete ui;
}

void transceiverAdjustments::setMaxPassband(quint16 maxHzAllowed)
{
    if( (maxHzAllowed <= 10E3) && (maxHzAllowed != 0) )
    {
        maxHz = maxHzAllowed;
        updatePassband(lastKnownPassband);
    }
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
        inner = qMax( 0, qMin(255,int (inner + shift)) );
        outer = qMax( 0, qMin(255,int (outer + shift)) );

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

void transceiverAdjustments::updatePassband(quint16 passbandHz)
{
    lastKnownPassband = passbandHz;
    float l = 2.0*passbandHz/maxHz;
#ifdef Q_OS_LINUX
    int val = exp10f(l);
#else
    int val = pow(10, l);
#endif

    //qDebug() << "Updating slider passband to " << passbandHz << "Hz using 1-100 value:" << val << "with l=" << l << "and max=" << maxHz;
    ui->passbandWidthSlider->blockSignals(true);
    ui->passbandWidthSlider->setValue(val);
    ui->passbandWidthSlider->blockSignals(false);
}

void transceiverAdjustments::on_resetPBTbtn_clicked()
{
    ui->TPBFInnerSlider->setValue(128);
    ui->TPBFOuterSlider->setValue(128);
    ui->IFShiftSlider->blockSignals(true);
    ui->IFShiftSlider->setValue(128);
    ui->IFShiftSlider->blockSignals(false);
}

void transceiverAdjustments::on_passbandWidthSlider_valueChanged(int value)
{
    // value is 1-100
    //float maxHz = 10E3;
    float l = log10f(value);
    float p = l*maxHz/2.0;
    quint16 pbHz = (quint16)p;
    if(pbHz != 0)
    {
        //qDebug() << "Setting passband, maxHZ" << maxHz << ", value: " << value << ", l:" << l << ", p:" << p << ", pbHz: " << pbHz;
        lastKnownPassband = pbHz;
        emit setPassband(pbHz);
    }
}
