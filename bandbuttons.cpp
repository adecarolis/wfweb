#include "bandbuttons.h"
#include "ui_bandbuttons.h"

bandbuttons::bandbuttons(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::bandbuttons)
{
    ui->setupUi(this);
    ui->bandStkLastUsedBtn->setVisible(false);
    ui->bandStkVoiceBtn->setVisible(false);
    ui->bandStkDataBtn->setVisible(false);
    ui->bandStkCWBtn->setVisible(false);
    this->setWindowTitle("Band Switcher");
}

bandbuttons::~bandbuttons()
{
    delete ui;
}

int bandbuttons::getBSRNumber()
{
    return ui->bandStkPopdown->currentIndex()+1;
}

void bandbuttons::acceptRigCaps(rigCapabilities rc)
{
    this->rigCaps = rc;
    qDebug(logGui()) << "Accepting new rigcaps into band buttons.";
    if(haveRigCaps)
        qDebug(logGui()) << "Accepting additional rigcaps into band buttons.";

    qDebug(logGui()) << "Bands in this rigcaps: ";
    for(size_t i=0; i < rigCaps.bands.size(); i++)
    {
        qDebug(logGui()) << "band[" << i << "]: " << (unsigned char)rigCaps.bands.at(i).band;
    }

    for(size_t i=0; i < 20; i++)
    {
        qDebug(logGui()) << "bsr[" << i << "]: " << (unsigned char)rigCaps.bsr[i];
    }

    setUIToRig();
    haveRigCaps = true;
}

void bandbuttons::setUIToRig()
{
    // Turn off each button first:
    hideButton(ui->band23cmbtn);
    hideButton(ui->band70cmbtn);
    hideButton(ui->band2mbtn);
    hideButton(ui->bandAirbtn);
    hideButton(ui->bandWFMbtn);
    hideButton(ui->band4mbtn);
    hideButton(ui->band6mbtn);

    hideButton(ui->band10mbtn);
    hideButton(ui->band12mbtn);
    hideButton(ui->band15mbtn);
    hideButton(ui->band17mbtn);
    hideButton(ui->band20mbtn);
    hideButton(ui->band30mbtn);
    hideButton(ui->band40mbtn);
    hideButton(ui->band60mbtn);
    hideButton(ui->band80mbtn);
    hideButton(ui->band160mbtn);

    hideButton(ui->band630mbtn);
    hideButton(ui->band2200mbtn);
    hideButton(ui->bandGenbtn);

    bandType bandSel;
    for(unsigned int i=0; i < rigCaps.bands.size(); i++)
    {
        bandSel = rigCaps.bands.at(i);
        switch(bandSel.band)
        {
            case(band23cm):
                showButton(ui->band23cmbtn);
                break;
            case(band70cm):
                showButton(ui->band70cmbtn);
                break;
            case(band2m):
                showButton(ui->band2mbtn);
                break;
            case(bandAir):
                showButton(ui->bandAirbtn);
                break;
            case(bandWFM):
                showButton(ui->bandWFMbtn);
                break;
            case(band4m):
                showButton(ui->band4mbtn);
                break;
            case(band6m):
                showButton(ui->band6mbtn);
                break;

            case(band10m):
                showButton(ui->band10mbtn);
                break;
            case(band12m):
                showButton(ui->band12mbtn);
                break;
            case(band15m):
                showButton(ui->band15mbtn);
                break;
            case(band17m):
                showButton(ui->band17mbtn);
                break;
            case(band20m):
                showButton(ui->band20mbtn);
                break;
            case(band30m):
                showButton(ui->band30mbtn);
                break;
            case(band40m):
                showButton(ui->band40mbtn);
                break;
            case(band60m):
                showButton(ui->band60mbtn);
                break;
            case(band80m):
                showButton(ui->band80mbtn);
                break;
            case(band160m):
                showButton(ui->band160mbtn);
                break;

            case(band630m):
                showButton(ui->band630mbtn);
                break;
            case(band2200m):
                showButton(ui->band2200mbtn);
                break;
            case(bandGen):
                showButton(ui->bandGenbtn);
                break;

            default:
                break;
        }
    }
}

void bandbuttons::showButton(QPushButton *b)
{
    b->setVisible(true);
}

void bandbuttons::hideButton(QPushButton *b)
{
    b->setHidden(true);
}

void bandbuttons::receiveBandStackReg(freqt freqGo, char mode, char filter, bool dataOn)
{
    // This function is not currently used, but perhaps it should be?
    // Issue: wfmain is also waiting for a BSR and acts upon it.
    if(waitingForBSR)
    {
        qDebug(logGui()) << "Received band stack register and was waiting for one.";
        emit issueCmdF(cmdSetFreq, freqGo);
        emit issueCmd(cmdSetMode, mode);
        //emit issueCmd(cmdSetFilter, filter); // TODO
        if(dataOn)
            emit issueDelayedCommand(cmdSetDataModeOn);
        else
            emit issueDelayedCommand(cmdSetDataModeOff);

        waitingForBSR = false;
    } else {
        qWarning(logGui()) << "Received a BSR but did not expect one.";
    }
    (void)filter;
}

void bandbuttons::bandStackBtnClick(availableBands band)
{
    if(haveRigCaps)
    {
        unsigned char bandRegister = rigCaps.bsr[band];
        if(bandRegister == 00)
        {
            qDebug(logGui()) << "requested to drop to band that does not have a BSR.";
            // TODO: Jump to reasonable frequency for the selected band
            jumpToBandWithoutBSR(band);
        } else {
            waitingForBSR = true;
            // TODO: request the BSR 1, 2, or 3
            emit issueCmd(cmdGetBandStackReg, (unsigned char)band);
        }
    } else {
        qWarning(logGui()) << "bandbuttons, Asked to go to a band but do not have rigCaps yet.";
    }
}

void bandbuttons::jumpToBandWithoutBSR(availableBands band)
{
    // Sometimes we do not have a BSR for these bands:
    freqt f;
    f.Hz = 0;

    switch(band)
    {
    case band2200m:
        f.Hz = 136 * 1E3;
        break;
    case band630m:
        f.Hz = 475 * 1E3;
        break;
    case band60m:
        f.Hz = (5.3305) * 1E6;
        break;
    case band4m:
        f.Hz = (70.200) * 1E6;
        break;
    case bandAir:
        f.Hz = 121.5 * 1E6;
        break;
    case bandGen:
        f.Hz = 10.0 * 1E6;
        break;
    case bandWFM:
        f.Hz = (100.1) * 1E6;
        break;
    default:
        qCritical(logGui()) << "Band " << (unsigned char) band << " not understood.";
        break;
    }
    if(f.Hz != 0)
    {
        emit issueCmdF(cmdSetFreq, f);
    }
}

void bandbuttons::on_band2200mbtn_clicked()
{
    bandStackBtnClick(band2200m);
}

void bandbuttons::on_band630mbtn_clicked()
{
    bandStackBtnClick(band630m);
}

void bandbuttons::on_band160mbtn_clicked()
{
    bandStackBtnClick(band160m);
}

void bandbuttons::on_band80mbtn_clicked()
{
    bandStackBtnClick(band80m);
}

void bandbuttons::on_band60mbtn_clicked()
{
    bandStackBtnClick(band60m);
}

void bandbuttons::on_band40mbtn_clicked()
{
    bandStackBtnClick(band40m);
}

void bandbuttons::on_band30mbtn_clicked()
{
    bandStackBtnClick(band30m);
}

void bandbuttons::on_band20mbtn_clicked()
{
    bandStackBtnClick(band20m);
}

void bandbuttons::on_band17mbtn_clicked()
{
    bandStackBtnClick(band17m);
}

void bandbuttons::on_band15mbtn_clicked()
{
    bandStackBtnClick(band15m);
}

void bandbuttons::on_band12mbtn_clicked()
{
    bandStackBtnClick(band12m);
}

void bandbuttons::on_band10mbtn_clicked()
{
    bandStackBtnClick(band10m);
}

void bandbuttons::on_band6mbtn_clicked()
{
    bandStackBtnClick(band6m);
}

void bandbuttons::on_band4mbtn_clicked()
{
    bandStackBtnClick(band4m);
}

void bandbuttons::on_band2mbtn_clicked()
{
    bandStackBtnClick(band2m);
}

void bandbuttons::on_band70cmbtn_clicked()
{
    bandStackBtnClick(band70cm);
}

void bandbuttons::on_band23cmbtn_clicked()
{
    bandStackBtnClick(band23cm);
}

void bandbuttons::on_bandWFMbtn_clicked()
{
    bandStackBtnClick(bandWFM);
}

void bandbuttons::on_bandAirbtn_clicked()
{
    bandStackBtnClick(bandAir);
}

void bandbuttons::on_bandGenbtn_clicked()
{
    bandStackBtnClick(bandGen);
}
