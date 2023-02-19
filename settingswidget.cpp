#include "settingswidget.h"
#include "ui_settingswidget.h"

#define setchk(a,b) quietlyUpdateCheckbox(a,b)

settingswidget::settingswidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::settingswidget)
{
    ui->setupUi(this);
    createSettingsListItems();
    populateComboBoxes();
}

settingswidget::~settingswidget()
{
    delete ui;
}

void settingswidget::createSettingsListItems()
{
    // Add items to the settings tab list widget
    ui->settingsList->addItem("Radio Access");     // 0
    ui->settingsList->addItem("User Interface");   // 1
    ui->settingsList->addItem("Radio Settings");   // 2
    ui->settingsList->addItem("Radio Server");     // 3
    ui->settingsList->addItem("External Control"); // 4
    ui->settingsList->addItem("DX Cluster");       // 5
    ui->settingsList->addItem("Experimental");     // 6
    //ui->settingsList->addItem("Audio Processing"); // 7
    ui->settingsStack->setCurrentIndex(0);
}

void settingswidget::populateComboBoxes()
{

    ui->baudRateCombo->insertItem(0, QString("115200"), 115200);
    ui->baudRateCombo->insertItem(1, QString("57600"), 57600);
    ui->baudRateCombo->insertItem(2, QString("38400"), 38400);
    ui->baudRateCombo->insertItem(3, QString("28800"), 28800);
    ui->baudRateCombo->insertItem(4, QString("19200"), 19200);
    ui->baudRateCombo->insertItem(5, QString("9600"), 9600);
    ui->baudRateCombo->insertItem(6, QString("4800"), 4800);
    ui->baudRateCombo->insertItem(7, QString("2400"), 2400);
    ui->baudRateCombo->insertItem(8, QString("1200"), 1200);
    ui->baudRateCombo->insertItem(9, QString("300"), 300);

    ui->meter2selectionCombo->addItem("None", meterNone);
    ui->meter2selectionCombo->addItem("SWR", meterSWR);
    ui->meter2selectionCombo->addItem("ALC", meterALC);
    ui->meter2selectionCombo->addItem("Compression", meterComp);
    ui->meter2selectionCombo->addItem("Voltage", meterVoltage);
    ui->meter2selectionCombo->addItem("Current", meterCurrent);
    ui->meter2selectionCombo->addItem("Center", meterCenter);
    ui->meter2selectionCombo->addItem("TxRxAudio", meterAudio);
    ui->meter2selectionCombo->addItem("RxAudio", meterRxAudio);
    ui->meter2selectionCombo->addItem("TxAudio", meterTxMod);


    ui->meter2selectionCombo->show();
    // prefs not valid yet ui->meter2selectionCombo->setCurrentIndex((int)prefs.meter2Type);

    ui->secondaryMeterSelectionLabel->show();




    ui->controlPortTxt->setValidator(new QIntValidator(this));
}

void settingswidget::on_settingsList_currentRowChanged(int currentRow)
{
    ui->settingsStack->setCurrentIndex(currentRow);
}

void settingswidget::acceptPreferencesPtr(preferences *pptr)
{
    if(pptr != NULL)
    {
        qDebug(logGui()) << "Accepting general preferences pointer into settings widget.";
        prefs = pptr;
        havePrefs = true;
    }
}

void settingswidget::acceptUdpPreferencesPtr(udpPreferences *upptr)
{
    if(upptr != NULL)
    {
        qDebug(logGui()) << "Accepting UDP preferences pointer into settings widget.";
        udpPrefs = upptr;
        haveUdpPrefs = true;
    }
}

void settingswidget::updateIfPrefs(int items)
{
    prefIfItem pif;
    if(items & (int)if_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)if_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating If pref" << (int)i;
            pif = (prefIfItem)i;
            updateIfPref(pif);
        }
    }


//    for(int i=0; 0x1 << i < (int)if_all; i++)
//    {
//        if(items & (0x1 << i))
//        {
//            qDebug(logGui()) << "Updating If pref" << (int)0;
//            pif = (prefIfItem)(0x1 << i);
//            updateIfPref(pif);
//        }
//    }
}

void settingswidget::updateRaPrefs(int items)
{
    prefRaItem pra;
    if(items & (int)ra_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)ra_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating Ra pref" << (int)i;
            pra = (prefRaItem)i;
            updateRaPref(pra);
        }
    }
}

void settingswidget::updateCtPrefs(int items)
{
    prefCtItem pct;
    if(items & (int)ct_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)ct_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating Ct pref" << (int)i;
            pct = (prefCtItem)i;
            updateCtPref(pct);
        }
    }
}

void settingswidget::updateLanPrefs(int items)
{
    prefLanItem plan;
    if(items & (int)l_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)l_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating Lan pref" << (int)i;
            plan = (prefLanItem)i;
            updateLanPref(plan);
        }
    }
}

void settingswidget::updateClusterPrefs(int items)
{
    prefClusterItem pcl;
    if(items & (int)cl_all)
    {
        items = 0xffffffff;
    }
    for(int i=0; i < (int)cl_all; i++)
    {
        if(items & (0x1 << i))
        {
            qDebug(logGui()) << "Updating Cluster pref" << (int)i;
            pcl = (prefClusterItem)i;
            updateClusterPref(pcl);
        }
    }
}

void settingswidget::updateIfPref(prefIfItem pif)
{
    updatingUIFromPrefs = true;
    switch(pif)
    {
    case if_useFullScreen:
        quietlyUpdateCheckbox(ui->fullScreenChk, prefs->useFullScreen);
        break;
    case if_useSystemTheme:
        quietlyUpdateCheckbox(ui->useSystemThemeChk, prefs->useSystemTheme);
        break;
    case if_drawPeaks:
        // depreciated;
        break;
    case if_underlayMode:
        updateUnderlayMode();
        break;
    case if_underlayBufferSize:
        quietlyUpdateSlider(ui->underlayBufferSlider, prefs->underlayBufferSize);
        break;
    case if_wfAntiAlias:
        quietlyUpdateCheckbox(ui->wfAntiAliasChk, prefs->wfAntiAlias);
        break;
    case if_wfInterpolate:
        quietlyUpdateCheckbox(ui->wfInterpolateChk, prefs->wfInterpolate);
        break;
    case if_wftheme:
        // Not handled in settings.
        break;
    case if_plotFloor:
        // Not handled in settings.
        break;
    case if_plotCeiling:
        // Not handled in settings.
        break;
    case if_stylesheetPath:
        // No UI element for this.
        break;
    case if_wflength:
        // Not handled in settings.
        break;
    case if_confirmExit:
        // No UI element for this.
        break;
    case if_confirmPowerOff:
        // No UI element for this.
        break;
    case if_meterKind:
    {
        int m = ui->meter2selectionCombo->findData(prefs->meter2Type);
        ui->meter2selectionCombo->setCurrentIndex(m);
        break;
    }
    case if_clickDragTuningEnable:
        quietlyUpdateCheckbox(ui->clickDragTuningEnableChk, prefs->clickDragTuningEnable);
        break;
    case if_currentColorPresetNumber:
        ui->colorPresetCombo->setCurrentIndex(prefs->currentColorPresetNumber);
        // activate? or done when prefs load? Maybe some of each?
        // TODO
        break;
    default:
        qWarning(logGui()) << "Did not understand if pref update item " << (int)pif;
        break;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateRaPref(prefRaItem pra)
{
    updatingUIFromPrefs = true;
    switch(pra)
    {
    case ra_radioCIVAddr:
        // It may be possible to ignore this value at this time.
        // TODO
        if(prefs->radioCIVAddr == 0)
        {
            ui->rigCIVaddrHexLine->setText("auto");
            ui->rigCIVaddrHexLine->setEnabled(false);
        } else {
            ui->rigCIVaddrHexLine->setEnabled(true);
            ui->rigCIVaddrHexLine->setText(QString("%1").arg(prefs->radioCIVAddr, 2, 16));
        }
        break;
    case ra_CIVisRadioModel:
        quietlyUpdateCheckbox(ui->useCIVasRigIDChk, prefs->CIVisRadioModel);
        break;
    case ra_forceRTSasPTT:
        quietlyUpdateCheckbox(ui->useRTSforPTTchk, prefs->forceRTSasPTT);
        break;
    case ra_polling_ms:
        if(prefs->polling_ms == 0)
        {
            // Automatic
            ui->pollingButtonGroup->blockSignals(true);
            ui->autoPollBtn->setChecked(true);
            ui->manualPollBtn->setChecked(false);
            ui->pollingButtonGroup->blockSignals(false);
            ui->pollTimeMsSpin->setEnabled(false);
        } else {
            // Manual
            ui->pollingButtonGroup->blockSignals(true);
            ui->autoPollBtn->setChecked(false);
            ui->manualPollBtn->setChecked(true);
            ui->pollingButtonGroup->blockSignals(false);
            ui->pollTimeMsSpin->blockSignals(true);
            ui->pollTimeMsSpin->setValue(prefs->polling_ms);
            ui->pollTimeMsSpin->blockSignals(false);
            ui->pollTimeMsSpin->setEnabled(true);
        }
        break;
    case ra_serialPortRadio:
    {
        int serialIndex = ui->serialDeviceListCombo->findText(prefs->serialPortRadio);
        if (serialIndex != -1) {
            ui->serialDeviceListCombo->setCurrentIndex(serialIndex);
        } else {
            // TODO
            qWarning(logGui()) << "Cannot find serial port in serial combo box.";
        }
        break;
    }
    case ra_serialPortBaud:
        ui->baudRateCombo->blockSignals(true);
        ui->baudRateCombo->setCurrentIndex(ui->baudRateCombo->findData(prefs->serialPortBaud));
        ui->baudRateCombo->blockSignals(false);
        break;
    case ra_virtualSerialPort:
    {
        int vspIndex = ui->vspCombo->findText(prefs->virtualSerialPort);
        if (vspIndex != -1) {
            ui->vspCombo->setCurrentIndex(vspIndex);
        }
        else
        {
            ui->vspCombo->addItem(prefs->virtualSerialPort);
            ui->vspCombo->setCurrentIndex(ui->vspCombo->count() - 1);
        }
        break;
    }
    case ra_localAFgain:
        // Not handled here.
        break;
    case ra_audioSystem:
        ui->audioSystemCombo->blockSignals(true);
        ui->audioSystemServerCombo->blockSignals(true);
        ui->audioSystemCombo->setCurrentIndex(prefs->audioSystem);
        ui->audioSystemServerCombo->setCurrentIndex(prefs->audioSystem);
        ui->audioSystemServerCombo->blockSignals(false);
        ui->audioSystemCombo->blockSignals(false);
        break;
    default:
        qWarning(logGui()) << "Cannot update ra pref" << (int)pra;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateCtPref(prefCtItem pct)
{
    updatingUIFromPrefs = true;

    switch(pct)
    {
    case ct_enablePTT:
        quietlyUpdateCheckbox(ui->pttEnableChk, prefs->enablePTT);
        break;
    case ct_niceTS:
        quietlyUpdateCheckbox(ui->tuningFloorZerosChk, prefs->niceTS);
        break;
    case ct_automaticSidebandSwitching:
        quietlyUpdateCheckbox(ui->autoSSBchk, prefs->automaticSidebandSwitching);
        break;
    case ct_enableUSBControllers:
        quietlyUpdateCheckbox(ui->enableUsbChk, prefs->enableUSBControllers);
        break;
    case ct_usbSensitivity:
        // No UI element for this.
        break;
    default:
        qWarning(logGui()) << "No UI element matches setting" << (int)pct;
        break;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateLanPref(prefLanItem plan)
{
    updatingUIFromPrefs = true;
    switch(plan)
    {
    case l_enableLAN:
        quietlyUpdateRadiobutton(ui->lanEnableBtn, prefs->enableLAN);
        break;
    case l_enableRigCtlD:
        quietlyUpdateCheckbox(ui->enableRigctldChk, prefs->enableRigCtlD);
        break;
    case l_rigCtlPort:
        ui->rigctldPortTxt->setText(QString::number(prefs->rigCtlPort));
        break;
    case l_tcpPort:
        ui->tcpServerPortTxt->setText(QString::number(prefs->tcpPort));
        break;
    case l_waterfallFormat:
        ui->waterfallFormatCombo->blockSignals(true);
        ui->waterfallFormatCombo->setCurrentIndex(prefs->waterfallFormat);
        ui->waterfallFormatCombo->blockSignals(false);
        break;
    default:
        qWarning(logGui()) << "Did not find matching preference for ui update:" << (int)plan;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateClusterPref(prefClusterItem pcl)
{
    updatingUIFromPrefs = true;
    // TODO, lots of work here....
    switch(pcl)
    {
    case cl_clusterUdpEnable:
        quietlyUpdateCheckbox(ui->clusterUdpEnable, prefs->clusterUdpEnable);
        break;
    case cl_clusterTcpEnable:
        quietlyUpdateCheckbox(ui->clusterTcpEnable, prefs->clusterTcpEnable);
        break;
    case cl_clusterUdpPort:
        ui->clusterUdpPortLineEdit->setText(QString::number(prefs->clusterUdpPort));
        break;
    case cl_clusterTcpServerName:
        break;
    case cl_clusterTcpUserName:
        break;
    case cl_clusterTcpPassword:
        break;
    case cl_clusterTimeout:
        break;
    case cl_clusterSkimmerSpotsEnable:
        quietlyUpdateCheckbox(ui->clusterSkimmerSpotsEnable, prefs->clusterSkimmerSpotsEnable);
        break;
    default:
        qWarning(logGui()) << "Did not find matching UI element for cluster preference " << (int)pcl;
        break;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateUdpPrefs(int items)
{
    udpPrefsItem upi;
    if(items & (int)u_all)
    {
        items = 0xffffffff;
    }
    for(int i=0; i < (int)u_all; i++)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating UDP preference " << i;
            upi = (udpPrefsItem)i;
            updateUdpPref(upi);
        }
    }
}

void settingswidget::updateUdpPref(udpPrefsItem upi)
{
    updatingUIFromPrefs = true;
    switch(upi)
    {
    case u_ipAddress:
        ui->ipAddressTxt->setEnabled(ui->lanEnableBtn->isChecked());
        ui->ipAddressTxt->setText(udpPrefs->ipAddress);
        break;
    case u_controlLANPort:
        ui->controlPortTxt->setEnabled(ui->lanEnableBtn->isChecked());
        ui->controlPortTxt->setText(QString("%1").arg(udpPrefs->controlLANPort));
        break;
    case u_serialLANPort:
        // Not used in the UI.
        break;
    case u_audioLANPort:
        // Not used in the UI.
        break;
    case u_username:
        ui->usernameTxt->setEnabled(ui->lanEnableBtn->isChecked());
        ui->usernameTxt->setText(QString("%1").arg(udpPrefs->username));
        break;
    case u_password:
        ui->passwordTxt->setEnabled(ui->lanEnableBtn->isChecked());
        ui->passwordTxt->setText(QString("%1").arg(udpPrefs->password));
        break;
    case u_clientName:
        // Not used in the UI.
        break;
    case u_waterfallFormat:
        // Not used in the UI.
        break;
    case u_halfDuplex:
        ui->audioDuplexCombo->setCurrentIndex((int)udpPrefs->halfDuplex);
        break;
    default:
        qWarning(logGui()) << "Did not find matching UI element for UDP pref item " << (int)upi;
        break;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateUnderlayMode()
{

    quietlyUpdateRadiobutton(ui->underlayNone, false);
    quietlyUpdateRadiobutton(ui->underlayPeakHold, false);
    quietlyUpdateRadiobutton(ui->underlayPeakBuffer, false);
    quietlyUpdateRadiobutton(ui->underlayAverageBuffer, false);

    switch(prefs->underlayMode) {
    case underlayNone:
        quietlyUpdateRadiobutton(ui->underlayNone, true);
        break;
    case underlayPeakHold:
        quietlyUpdateRadiobutton(ui->underlayPeakHold, true);
        break;
    case underlayPeakBuffer:
        quietlyUpdateRadiobutton(ui->underlayPeakBuffer, true);
        break;
    case underlayAverageBuffer:
        quietlyUpdateRadiobutton(ui->underlayAverageBuffer, true);
        break;
    default:
        qWarning() << "Do not understand underlay mode: " << (unsigned int) prefs->underlayMode;
    }
}

void settingswidget::updateAllPrefs()
{
    // DEPRECIATED


    // Review all the preferences. This is intended to be called
    // after new settings are loaded in.
    updatingUIFromPrefs = true;
    ui->fullScreenChk->setChecked(prefs->useFullScreen);
    ui->useSystemThemeChk->setChecked(prefs->useSystemTheme);
    //drawPeaks not used
    switch(prefs->underlayMode) {
    case underlayNone:
        ui->underlayNone->setChecked(true);
        break;
    case underlayPeakHold:
        ui->underlayPeakHold->setChecked(true);
        break;
    case underlayPeakBuffer:
        ui->underlayPeakBuffer->setChecked(true);
        break;
    case underlayAverageBuffer:
        ui->underlayAverageBuffer->setChecked(true);
        break;
    default:
        qWarning() << "Do not understand underlay mode: " << (unsigned int) prefs->underlayMode;
    }
    quietlyUpdateSlider(ui->underlayBufferSlider, prefs->underlayBufferSize);
    ui->wfAntiAliasChk->setChecked(prefs->wfAntiAlias);
    ui->wfInterpolateChk->setChecked(prefs->wfInterpolate);

    updatingUIFromPrefs = false;
}

void settingswidget::quietlyUpdateSlider(QSlider *sl, int val)
{
    sl->blockSignals(true);
    if( (val >= sl->minimum()) && (val <= sl->maximum()) )
        sl->setValue(val);
    sl->blockSignals(false);
}

void settingswidget::quietlyUpdateCheckbox(QCheckBox *cb, bool isChecked)
{
    cb->blockSignals(true);
    cb->setChecked(isChecked);
    cb->blockSignals(false);
}

void settingswidget::quietlyUpdateRadiobutton(QRadioButton *rb, bool isChecked)
{
    rb->blockSignals(true);
    rb->setChecked(isChecked);
    rb->blockSignals(false);
}

void settingswidget::on_lanEnableBtn_clicked(bool checked)
{
    // TODO: prefs.enableLAN = checked;
    // TOTO? ui->connectBtn->setEnabled(true);
    ui->ipAddressTxt->setEnabled(checked);
    ui->controlPortTxt->setEnabled(checked);
    ui->usernameTxt->setEnabled(checked);
    ui->passwordTxt->setEnabled(checked);
    ui->audioRXCodecCombo->setEnabled(checked);
    ui->audioTXCodecCombo->setEnabled(checked);
    ui->audioSampleRateCombo->setEnabled(checked);
    ui->rxLatencySlider->setEnabled(checked);
    ui->txLatencySlider->setEnabled(checked);
    ui->rxLatencyValue->setEnabled(checked);
    ui->txLatencyValue->setEnabled(checked);
    ui->audioOutputCombo->setEnabled(checked);
    ui->audioInputCombo->setEnabled(checked);
    ui->baudRateCombo->setEnabled(!checked);
    ui->serialDeviceListCombo->setEnabled(!checked);
    ui->serverRXAudioInputCombo->setEnabled(!checked);
    ui->serverTXAudioOutputCombo->setEnabled(!checked);
    if(checked)
    {
        //showStatusBarText("After filling in values, press Save Settings.");
    }
    prefs->enableLAN = checked;
    // TODO: emit widgetChangedPrefs(l_enableLAN);
}
