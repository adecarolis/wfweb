#include "repeatersetup.h"
#include "ui_repeatersetup.h"

repeaterSetup::repeaterSetup(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::repeaterSetup)
{
    ui->setupUi(this);

    this->setObjectName("RepeaterSetup");
    queue = cachingQueue::getInstance();
    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));

    ui->autoTrackLiveBtn->setEnabled(false); // until we set split enabled.
    ui->warningFMLabel->setVisible(false);
}

repeaterSetup::~repeaterSetup()
{
    // Trying this for more consistent destruction
    delete ui;

}

void repeaterSetup::receiveDuplexMode(duplexMode_t dm)
{
    currentdm = dm;
    ui->splitEnableChk->blockSignals(true);
    switch(dm)
    {
        case dmSplitOff:
            ui->splitOffBtn->setChecked(true);
            ui->autoTrackLiveBtn->setDisabled(true);
            break;
        case dmSplitOn:
            ui->splitEnableChk->setChecked(true);
            ui->rptSimplexBtn->setChecked(false);
            ui->rptDupPlusBtn->setChecked(false);
            ui->autoTrackLiveBtn->setEnabled(true);
            ui->rptDupMinusBtn->setChecked(false);
            break;
        case dmSimplex:
            ui->rptSimplexBtn->setChecked(true);
            //ui->splitEnableChk->setChecked(false);
            ui->autoTrackLiveBtn->setDisabled(true);
            break;
        case dmDupPlus:
            ui->rptDupPlusBtn->setChecked(true);
            //ui->splitEnableChk->setChecked(false);
            ui->autoTrackLiveBtn->setDisabled(true);
            break;
        case dmDupMinus:
            ui->rptDupMinusBtn->setChecked(true);
            //ui->splitEnableChk->setChecked(false);
            ui->autoTrackLiveBtn->setDisabled(true);
            break;
        default:
            qDebug() << "Did not understand duplex/split/repeater value of " << (quint8)dm;
            break;
    }
    ui->splitEnableChk->blockSignals(false);
}

void repeaterSetup::handleRptAccessMode(rptAccessTxRx_t tmode)
{
    // ratrXY
    // X = Transmit (T)one or (N)one or (D)CS
    // Y = Receive (T)sql or (N)one or (D)CS
    switch(tmode)
    {
    case ratrNN:
        ui->toneNone->setChecked(true);
        break;
    case ratrTT:
    case ratrNT:
        ui->toneTSQL->setChecked(true);
        break;
    case ratrTN:
        ui->toneTone->setChecked(true);
        break;
    case ratrDD:
        ui->toneDTCS->setChecked(true);
        break;
    case ratrTONEoff:
        ui->toneTone->setChecked(false);
        break;
    case ratrTONEon:
        ui->toneTone->setChecked(true);
        break;
    case ratrTSQLoff:
        ui->toneTSQL->setChecked(false);
        break;
    case ratrTSQLon:
        ui->toneTSQL->setChecked(true);
        break;
    default:
        break;
    }
    if( !ui->toneTSQL->isChecked() && !ui->toneTone->isChecked() && !ui->toneDTCS->isChecked())
    {
        ui->toneNone->setChecked(true);
    }
}

void repeaterSetup::handleTone(quint16 tone)
{
    ui->rptToneCombo->setCurrentIndex(ui->rptToneCombo->findData(tone));
}

void repeaterSetup::handleTSQL(quint16 tsql)
{
    ui->rptTSQLCombo->setCurrentIndex(ui->rptTSQLCombo->findData(tsql));
}

void repeaterSetup::handleDTCS(quint16 dcode, bool tinv, bool rinv)
{
    ui->rptDTCSCombo->setCurrentIndex(ui->rptDTCSCombo->findData(dcode));
    ui->rptDTCSInvertTx->setChecked(tinv);
    ui->rptDTCSInvertRx->setChecked(rinv);
}

void repeaterSetup::handleUpdateCurrentMainFrequency(freqt mainfreq)
{
    if(mainfreq.VFO == inactiveVFO)
        return;

    haveReceivedFrequency = true;
    if(amTransmitting)
        return;

    // Track if autoTrack enabled, split enabled, and there's a split defined.
    if(ui->autoTrackLiveBtn->isChecked() && (currentdm == dmSplitOn) && !ui->splitOffsetEdit->text().isEmpty())
    {
        if(currentMainFrequency.Hz != mainfreq.Hz)
        {
            this->currentMainFrequency = mainfreq;
            if(!ui->splitTransmitFreqEdit->hasFocus())
            {
                if(usedPlusSplit)
                {
                    on_splitPlusButton_clicked();
                } else {
                    on_splitMinusBtn_clicked();
                }
            }
        }
        if(ui->setSplitRptrToneChk->isChecked())
        {
            // TODO, not really needed if the op
            // just sets the tone when needed, as it will do both bands.
        }
    }
    this->currentMainFrequency = mainfreq;
}

void repeaterSetup::handleUpdateCurrentMainMode(modeInfo m)
{
    // Used to set the secondary VFO to the same mode
    // (generally FM)
    // NB: We don't accept values during transmit as they
    // may represent the inactive VFO
    if(!amTransmitting && m.mk != modeUnknown)
    {
        this->currentModeMain = m;
        this->modeTransmitVFO = m;
        this->modeTransmitVFO.VFO = inactiveVFO;
        if(m.mk == modeFM)
            ui->warningFMLabel->setVisible(false);
        else
            ui->warningFMLabel->setVisible(true);
    }
}

void repeaterSetup::handleRptOffsetFrequency(freqt f)
{
    // Called when a new offset is available from the radio.
    QString offsetstr = QString::number(f.Hz / double(1E6), 'f', 4);

    if(!ui->rptrOffsetEdit->hasFocus())
    {
        ui->rptrOffsetEdit->setText(offsetstr);
        currentOffset = f;
    }
}

void repeaterSetup::handleTransmitStatus(bool amTransmitting)
{
    this->amTransmitting = amTransmitting;
}

void repeaterSetup::showEvent(QShowEvent *event)
{
    if(haveRig) {
        queue->add(priorityImmediate,funcSplitStatus,false,false);
        if(rigCaps->commands.contains(funcToneSquelchType))
            queue->add(priorityImmediate,funcReadFreqOffset,false,false);
        QMainWindow::showEvent(event);
    }
    (void)event;
}

void repeaterSetup::on_splitEnableChk_clicked()
{
    queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmSplitOn),false));
    ui->autoTrackLiveBtn->setEnabled(true);

    if(ui->autoTrackLiveBtn->isChecked() && !ui->splitOffsetEdit->text().isEmpty())
    {
        if(usedPlusSplit)
        {
            on_splitPlusButton_clicked();
        } else {
            on_splitMinusBtn_clicked();
        }
    }
}

void repeaterSetup::on_splitOffBtn_clicked()
{
    queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmSplitOff),false));
    ui->autoTrackLiveBtn->setDisabled(true);
}

void repeaterSetup::on_rptSimplexBtn_clicked()
{
    // Simplex
    //queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmSplitOn),false));
    //if(rigCaps->commands.contains(funcToneSquelchType))
    //{
        //queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmDupAutoOff),false));
        queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmSimplex),false));
    //}
}

void repeaterSetup::on_rptDupPlusBtn_clicked()
{
    // DUP+
    //queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmDupAutoOff),false));
    queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmDupPlus),false));
}

void repeaterSetup::on_rptDupMinusBtn_clicked()
{
    // DUP-
    //queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmDupAutoOff),false));
    queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmDupMinus),false));
}

void repeaterSetup::on_rptAutoBtn_clicked()
{
    // Auto Rptr (enable this feature)
    // TODO: Hide an AutoOff button somewhere for non-US users
    queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(dmDupAutoOn),false));
}

void repeaterSetup::on_rptToneCombo_activated(int tindex)
{
    toneInfo rt;
    for (const auto &ti: rigCaps->ctcss)
    {
        if (ti.tone == ui->rptToneCombo->itemData(tindex).toUInt()) {
            rt = ti;
            break;
        }
    }

    bool updateSub = ui->setSplitRptrToneChk->isEnabled() && ui->setSplitRptrToneChk->isChecked();

    queue->add(priorityImmediate,queueItem(funcToneFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));
    if(updateSub)
    {
        rt.useSecondaryVFO = true;
        queue->add(priorityImmediate,queueItem(funcToneFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));
    }
}

void repeaterSetup::on_rptTSQLCombo_activated(int tindex)
{
    toneInfo rt;
    for (const auto &ti: rigCaps->ctcss)
    {
        if (ti.tone == ui->rptTSQLCombo->itemData(tindex).toUInt()) {
            rt = ti;
            break;
        }
    }

    bool updateSub = ui->setSplitRptrToneChk->isEnabled() && ui->setSplitRptrToneChk->isChecked();

    queue->add(priorityImmediate,queueItem(funcTSQLFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));
    if(updateSub)
    {
        rt.useSecondaryVFO = true;
        queue->add(priorityImmediate,queueItem(funcTSQLFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));
    }
}
void repeaterSetup::on_rptDTCSCombo_activated(int index)
{
    toneInfo tone;
    for (const auto &ti: rigCaps->dtcs)
    {
        if (ti.tone == (quint16)ui->rptDTCSCombo->itemData(index).toUInt()) {
            tone = ti;
            break;
        }
    }

    tone.tinv = ui->rptDTCSInvertTx->isChecked();
    tone.rinv = ui->rptDTCSInvertRx->isChecked();
    queue->add(priorityImmediate,queueItem(funcDTCSCode,QVariant::fromValue<toneInfo>(tone),false));
}

void repeaterSetup::setRptAccessMode(rptrAccessData rd)
{
    if (rigCaps->commands.contains(funcToneSquelchType)) {
        queue->add(priorityImmediate,queueItem(funcToneSquelchType,QVariant::fromValue<rptrAccessData>(rd),false));
    } else {
        if(rd.accessMode == ratrTN) {
            // recuring=false, vfo if rd.useSEcondaryVFO
            queue->add(priorityImmediate,queueItem(funcRepeaterTone, QVariant::fromValue<bool>(true), false, rd.useSecondaryVFO));
        } else if (rd.accessMode == ratrTT) {
            queue->add(priorityImmediate,queueItem(funcRepeaterTSQL, QVariant::fromValue<bool>(true), false, rd.useSecondaryVFO));
        } else if (rd.accessMode == ratrNN) {
            queue->add(priorityImmediate,queueItem(funcRepeaterTone, QVariant::fromValue<bool>(false), false, rd.useSecondaryVFO));
            queue->add(priorityImmediate,queueItem(funcRepeaterTSQL, QVariant::fromValue<bool>(false), false, rd.useSecondaryVFO));
        }
    }
}

void repeaterSetup::on_toneNone_clicked()
{
    rptAccessTxRx_t rm;
    rptrAccessData rd;
    rm = ratrNN;
    rd.accessMode = rm;
    setRptAccessMode(rd);
    bool updateSub = ui->setSplitRptrToneChk->isEnabled() && ui->setSplitRptrToneChk->isChecked();

    if(updateSub)
    {
        rd.useSecondaryVFO = true;
        setRptAccessMode(rd);
    }
}

void repeaterSetup::on_toneTone_clicked()
{
    rptAccessTxRx_t rm;
    rptrAccessData rd;
    rm = ratrTN;
    rd.accessMode = rm;
    toneInfo rt;
    for (const auto &ti: rigCaps->ctcss)
    {
        if (ti.tone == (quint16)ui->rptDTCSCombo->currentData().toUInt())
        {
            rt = ti;
            break;
        }
    }

    setRptAccessMode(rd);
    queue->add(priorityImmediate,queueItem(funcToneFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));

    bool updateSub = ui->setSplitRptrToneChk->isEnabled() && ui->setSplitRptrToneChk->isChecked();

    if(updateSub)
    {
        rd.useSecondaryVFO = true;
        rt.useSecondaryVFO = true;
        setRptAccessMode(rd);
        queue->add(priorityImmediate,queueItem(funcToneFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));
    }
}

void repeaterSetup::on_toneTSQL_clicked()
{
    rptAccessTxRx_t rm;
    rptrAccessData rd;
    rm = ratrTT;
    toneInfo rt;
    for (const auto &ti: rigCaps->ctcss)
    {
        if (ti.tone == (quint16)ui->rptDTCSCombo->currentData().toUInt())
        {
            rt = ti;
            break;
        }
    }
    rd.accessMode = rm;
    setRptAccessMode(rd);
    queue->add(priorityImmediate,queueItem(funcTSQLFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));
    bool updateSub = ui->setSplitRptrToneChk->isEnabled() && ui->setSplitRptrToneChk->isChecked();

    if(updateSub)
    {
        rd.useSecondaryVFO = true;
        rt.useSecondaryVFO = true;
        setRptAccessMode(rd);
        queue->add(priorityImmediate,queueItem(funcTSQLFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));
    }
}

void repeaterSetup::on_toneDTCS_clicked()
{
    rptrAccessData rd;

    rd.accessMode = ratrDD;
    setRptAccessMode(rd);
    toneInfo tone;
    for (const auto &ti: rigCaps->dtcs)
    {
        if (ti.tone == (quint16)ui->rptDTCSCombo->currentData().toUInt())
        {
            tone = ti;
            break;
        }
    }
    tone.tinv = ui->rptDTCSInvertTx->isChecked();
    tone.rinv = ui->rptDTCSInvertRx->isChecked();
    queue->add(priorityImmediate,queueItem(funcDTCSCode,QVariant::fromValue<toneInfo>(tone),false));
    // TODO: DTCS with subband
}

quint64 repeaterSetup::getFreqHzFromKHzString(QString khz)
{
    // This function takes a string containing a number in KHz,
    // and creates an accurate quint64 in Hz.
    quint64 fhz = 0;
    bool ok = true;
    if(khz.isEmpty())
    {
        qWarning() << "KHz offset was empty!";
        return fhz;
    }
    if(khz.contains("."))
    {
        // "600.245" becomes "600"
        khz.chop(khz.indexOf("."));
    }
    fhz = 1E3 * khz.toUInt(&ok);
    if(!ok)
    {
        qWarning() << "Could not understand user KHz text";
    }
    return fhz;
}

quint64 repeaterSetup::getFreqHzFromMHzString(QString MHz)
{
    // This function takes a string containing a number in KHz,
    // and creates an accurate quint64 in Hz.
    quint64 fhz = 0;
    bool ok = true;
    if(MHz.isEmpty())
    {
        qWarning() << "MHz string was empty!";
        return fhz;
    }
    if(MHz.contains("."))
    {
        int decimalPtIndex = MHz.indexOf(".");
        // "29.623"
        // indexOf(".") = 2
        // length = 6
        // We want the right 4xx 3 characters.
        QString KHz = MHz.right(MHz.length() - decimalPtIndex - 1);
        MHz.chop(MHz.length() - decimalPtIndex);
        if(KHz.length() != 6)
        {
            QString zeros = QString("000000");
            zeros.chop(KHz.length());
            KHz.append(zeros);
        }
        //qInfo() << "KHz string: " << KHz;
        fhz = MHz.toUInt(&ok) * 1E6; if(!ok) goto handleError;
        fhz += KHz.toUInt(&ok) * 1; if(!ok) goto handleError;
        //qInfo() << "Fhz: " << fhz;
    } else {
        // Frequency was already MHz (unlikely but what can we do?)
        fhz = MHz.toUInt(&ok) * 1E6; if(!ok) goto handleError;
    }
    return fhz;

handleError:
    qWarning() << "Could not understand user MHz text " << MHz;
    return 0;
}

void repeaterSetup::on_splitPlusButton_clicked()
{
    quint64 hzOffset = getFreqHzFromKHzString(ui->splitOffsetEdit->text());
    quint64 txfreqhz;
    freqt f;
    QString txString;
    if(!haveReceivedFrequency) {
        qWarning(logRptr()) << "Cannot compute offset without first receiving a frequency.";
        return;
    }
    if(hzOffset)
    {
        txfreqhz = currentMainFrequency.Hz + hzOffset;
        f.Hz = txfreqhz;
        f.VFO = inactiveVFO;
        f.MHzDouble = f.Hz/1E6;
        txString = QString::number(f.Hz / double(1E6), 'f', 6);
        ui->splitTransmitFreqEdit->setText(txString);
        usedPlusSplit = true;

    } else {
        qWarning(logRptr()) << "Cannot set split using supplied offset value.";
        return;
    }
    if(ui->splitEnableChk->isChecked() || ui->quickSplitChk->isChecked()) {
        queue->add(priorityImmediate,queueItem(queue->getVfoCommand((rigCaps->numVFO>1)?vfoB:vfoSub,uchar(rigCaps->numReceiver>1),true).freqFunc,
                                                QVariant::fromValue<freqt>(f),false,uchar(uchar(rigCaps->numReceiver>1))));

        //Not sure how to do this or if it is needed? M0VSE
        //queue->add(priorityImmediate,queueItem(funcModeSet,QVariant::fromValue<modeInfo>(modeTransmitVFO)));
    } else {
        qWarning(logRptr()) << "Not setting transmit frequency until split mode is enabled.";
    }
}

void repeaterSetup::on_splitMinusBtn_clicked()
{
    quint64 hzOffset = getFreqHzFromKHzString(ui->splitOffsetEdit->text());
    quint64 txfreqhz;
    freqt f;
    QString txString;
    if(!haveReceivedFrequency) {
        qWarning(logRptr()) << "Cannot compute offset without first receiving a frequency.";
        return;
    }
    if(hzOffset)
    {
        txfreqhz = currentMainFrequency.Hz - hzOffset;
        f.Hz = txfreqhz;
        f.VFO = inactiveVFO;
        f.MHzDouble = f.Hz/1E6;
        txString = QString::number(f.Hz / double(1E6), 'f', 6);
        ui->splitTransmitFreqEdit->setText(txString);
        usedPlusSplit = false;
    } else {
        qWarning(logRptr()) << "Cannot set split using supplied offset value.";
        return;
    }

    if(ui->splitEnableChk->isChecked() || ui->quickSplitChk->isChecked()) {
        queue->add(priorityImmediate,queueItem(queue->getVfoCommand((rigCaps->numVFO>1)?vfoB:vfoSub,uchar(rigCaps->numReceiver>1),true).freqFunc,
                                                QVariant::fromValue<freqt>(f),false,uchar(uchar(rigCaps->numReceiver>1))));
        // Not sure how to do this M0VSE?
        //queue->add(priorityImmediate,queueItem(funcModeSet,QVariant::fromValue<modeInfo>(modeTransmitVFO),false));
    } else {
        qWarning(logRptr()) << "Not setting transmit frequency until split mode is enabled.";
        return;
    }
}

void repeaterSetup::on_splitTxFreqSetBtn_clicked()
{
    if(!haveReceivedFrequency) {
        qWarning(logRptr()) << "Cannot compute offset without first receiving a frequency.";
        return;
    }
    quint64 fHz = getFreqHzFromMHzString(ui->splitTransmitFreqEdit->text());
    freqt f;
    if(fHz)
    {
        f.Hz = fHz;
        f.VFO = inactiveVFO;
        f.MHzDouble = f.Hz/1E6;
        queue->add(priorityImmediate,queueItem(queue->getVfoCommand((rigCaps->numVFO>1)?vfoB:vfoSub,uchar(rigCaps->numReceiver>1),true).freqFunc,
                                                QVariant::fromValue<freqt>(f),false,uchar(uchar(rigCaps->numReceiver>1))));

        //queue->add(priorityImmediate,queueItem(funcModeSet,QVariant::fromValue<modeInfo>(modeTransmitVFO),false,0));
    }
}

void repeaterSetup::on_splitTransmitFreqEdit_returnPressed()
{
    this->on_splitTxFreqSetBtn_clicked();
    ui->splitTransmitFreqEdit->clearFocus();
}

void repeaterSetup::on_selABtn_clicked()
{
    vfo_t v = vfoA;
    queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(v),false));
}

void repeaterSetup::on_selBBtn_clicked()
{
    vfo_t v = vfoB;
    queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(v),false));
}

void repeaterSetup::on_aEqBBtn_clicked()
{
    queue->add(priorityImmediate,funcVFOEqualAB,false,false);
}

void repeaterSetup::on_swapABBtn_clicked()
{
    queue->add(priorityImmediate,funcVFOSwapAB,false,false);
}

void repeaterSetup::on_selMainBtn_clicked()
{
    vfo_t v = vfoMain;
    queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(v),false));
}

void repeaterSetup::on_selSubBtn_clicked()
{
    vfo_t v = vfoSub;
    queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(v),false));
}

void repeaterSetup::on_mEqSBtn_clicked()
{
    queue->add(priorityImmediate,funcVFOEqualMS,false,false);
}

void repeaterSetup::on_swapMSBtn_clicked()
{
    queue->add(priorityImmediate,funcVFOSwapMS,false,false);
}

void repeaterSetup::on_setToneSubVFOBtn_clicked()
{
    // Perhaps not needed
    // Set the secondary VFO to the selected tone
    // TODO: DTCS
    toneInfo rt;
    for (const auto &ti: rigCaps->ctcss)
    {
        if (ti.tone == (quint16)ui->rptDTCSCombo->currentData().toUInt())
        {
            rt = ti;
            break;
        }
    }
    rt.useSecondaryVFO = true;
    queue->add(priorityImmediate,queueItem(funcToneFreq,QVariant::fromValue<toneInfo>(rt),false, rt.useSecondaryVFO));
}

void repeaterSetup::on_setRptrSubVFOBtn_clicked()
{
    // Perhaps not needed
    // Set the secondary VFO to the selected repeater mode
    rptrAccessData rd;
    rd.useSecondaryVFO = true;

    if(ui->toneTone->isChecked())
        rd.accessMode=ratrTN;
    if(ui->toneNone->isChecked())
        rd.accessMode=ratrNN;
    if(ui->toneTSQL->isChecked())
        rd.accessMode=ratrTT;
    if(ui->toneDTCS->isChecked())
        rd.accessMode=ratrDD;

    setRptAccessMode(rd);
}

void repeaterSetup::on_rptrOffsetSetBtn_clicked()
{
    freqt f;
    f.Hz = getFreqHzFromMHzString(ui->rptrOffsetEdit->text());
    f.MHzDouble = f.Hz/1E6;
    f.VFO=activeVFO;
    if(f.Hz != 0)
    {
        queue->add(priorityImmediate,queueItem(funcSendFreqOffset,QVariant::fromValue<freqt>(f),false));
    } else {
        qWarning() << "Could not convert frequency text of repeater offset to integer.";
    }
    ui->rptrOffsetEdit->clearFocus();
}

void repeaterSetup::on_rptrOffsetEdit_returnPressed()
{
    this->on_rptrOffsetSetBtn_clicked();
}

void repeaterSetup::on_setSplitRptrToneChk_clicked(bool checked)
{
    if(checked)
    {
        on_setRptrSubVFOBtn_clicked();
        on_setToneSubVFOBtn_clicked();
    }
}

void repeaterSetup::on_quickSplitChk_clicked(bool checked)
{
    queue->add(priorityImmediate,queueItem(funcQuickSplit,QVariant::fromValue<bool>(checked),false));
}

void repeaterSetup::receiveRigCaps(rigCapabilities* rig)
{
    qInfo() << "Receiving rigcaps into repeater setup.";
    this->rigCaps = rig;
    if (rig != Q_NULLPTR)
    {
        qInfo() << "repeaterSetup got rigcaps for:" << rig->modelName;

        if(rig->commands.contains(funcRepeaterTone)) {
            ui->rptToneCombo->setDisabled(false);
            ui->toneTone->setDisabled(false);
        } else {
            ui->rptToneCombo->setDisabled(true);
            ui->toneTone->setDisabled(true);
        }

        if(rig->commands.contains(funcRepeaterTSQL)) {
            ui->toneTSQL->setDisabled(false);
        } else {
            ui->toneTSQL->setDisabled(true);
        }

        if(rig->commands.contains(funcToneSquelchType))
        {
            ui->rptToneCombo->setDisabled(false);
            ui->toneTone->setDisabled(false);
            ui->toneTSQL->setDisabled(false);
        }

        if(rig->commands.contains(funcRepeaterDTCS))
        {
            ui->rptDTCSCombo->setDisabled(false);
            ui->toneDTCS->setDisabled(false);
            ui->rptDTCSInvertRx->setDisabled(false);
            ui->rptDTCSInvertTx->setDisabled(false);
        } else {
            ui->rptDTCSCombo->setDisabled(true);
            ui->toneDTCS->setDisabled(true);
            ui->rptDTCSInvertRx->setDisabled(true);
            ui->rptDTCSInvertTx->setDisabled(true);
        }
        if(rig->commands.contains(funcVFOEqualAB))
        {
            ui->selABtn->setDisabled(false);
            ui->selBBtn->setDisabled(false);
            ui->aEqBBtn->setDisabled(false);
            ui->swapABBtn->setDisabled(false);
        } else {
            ui->selABtn->setDisabled(true);
            ui->selBBtn->setDisabled(true);
            ui->aEqBBtn->setDisabled(true);
            ui->swapABBtn->setDisabled(true);
        }
        if(rig->commands.contains(funcVFOEqualMS))
        {
            ui->selMainBtn->setDisabled(false);
            ui->selSubBtn->setDisabled(false);
            ui->mEqSBtn->setDisabled(false);
            ui->swapMSBtn->setDisabled(false);
        } else {
            ui->selMainBtn->setDisabled(true);
            ui->selSubBtn->setDisabled(true);
            ui->mEqSBtn->setDisabled(true);
            ui->swapMSBtn->setDisabled(true);
        }
        if(rig->commands.contains(funcVFOEqualAB) && rig->commands.contains(funcVFOEqualMS))
        {
            // Rigs that have both AB and MS
            // do not have a swap AB command.
            //ui->swapABBtn->setDisabled(true);
        }

        bool mainSub = rig->commands.contains(funcVFOMainSelect);
        if(mainSub)
        {
            ui->setRptrSubVFOBtn->setEnabled(true);
            ui->setToneSubVFOBtn->setEnabled(true);
            ui->setSplitRptrToneChk->setEnabled(true);
        } else {
            ui->setRptrSubVFOBtn->setDisabled(true);
            ui->setToneSubVFOBtn->setDisabled(true);
            ui->setSplitRptrToneChk->setDisabled(true);
        }

        bool rpt = rig->commands.contains(funcToneSquelchType);
        ui->rptAutoBtn->setEnabled(rpt);
        ui->rptDupMinusBtn->setEnabled(rpt);
        ui->rptDupPlusBtn->setEnabled(rpt);
        ui->rptSimplexBtn->setEnabled(rpt);
        ui->rptrOffsetEdit->setEnabled(rpt);
        ui->rptrOffsetSetBtn->setEnabled(rpt);
        ui->setToneSubVFOBtn->setEnabled(mainSub);
        ui->setRptrSubVFOBtn->setEnabled(mainSub);
        ui->quickSplitChk->setVisible(rig->commands.contains(funcQuickSplit));

        // Populate tones:
        ui->rptToneCombo->clear();
        ui->rptTSQLCombo->clear();
        for (const auto& t: rigCaps->ctcss)
        {
            ui->rptToneCombo->addItem(t.name, t.tone);
            ui->rptTSQLCombo->addItem(t.name, t.tone);
        }

        ui->rptDTCSCombo->clear();
        for (const auto& t: rigCaps->dtcs)
        {
            ui->rptDTCSCombo->addItem(t.name, t.tone);
        }

    }
}

void repeaterSetup::receiveQuickSplit(bool on)
{
    ui->quickSplitChk->blockSignals(true);
    ui->quickSplitChk->setChecked(on);
    ui->quickSplitChk->blockSignals(false);
}
