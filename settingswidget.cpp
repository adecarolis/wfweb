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

// Startup:
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
    ui->baudRateCombo->blockSignals(true);
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
    ui->baudRateCombo->blockSignals(false);

    ui->meter2selectionCombo->blockSignals(true);
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
    ui->meter2selectionCombo->blockSignals(false);

    ui->secondaryMeterSelectionLabel->show();

    ui->audioRXCodecCombo->blockSignals(true);
    ui->audioRXCodecCombo->addItem("LPCM 1ch 16bit", 4);
    ui->audioRXCodecCombo->addItem("LPCM 1ch 8bit", 2);
    ui->audioRXCodecCombo->addItem("uLaw 1ch 8bit", 1);
    ui->audioRXCodecCombo->addItem("LPCM 2ch 16bit", 16);
    ui->audioRXCodecCombo->addItem("uLaw 2ch 8bit", 32);
    ui->audioRXCodecCombo->addItem("PCM 2ch 8bit", 8);
    ui->audioRXCodecCombo->addItem("Opus 1ch", 64);
    ui->audioRXCodecCombo->addItem("Opus 2ch", 128);
    ui->audioRXCodecCombo->blockSignals(false);

    ui->audioTXCodecCombo->blockSignals(true);
    ui->audioTXCodecCombo->addItem("LPCM 1ch 16bit", 4);
    ui->audioTXCodecCombo->addItem("LPCM 1ch 8bit", 2);
    ui->audioTXCodecCombo->addItem("uLaw 1ch 8bit", 1);
    ui->audioTXCodecCombo->addItem("Opus 1ch", 64);
    ui->audioTXCodecCombo->blockSignals(false);

    ui->controlPortTxt->setValidator(new QIntValidator(this));

    ui->modInputData2ComboText->setVisible(false);
    ui->modInputData2Combo->setVisible(false);
    ui->modInputData3ComboText->setVisible(false);
    ui->modInputData3Combo->setVisible(false);
}

// Updating Preferences:
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

void settingswidget::acceptServerConfig(SERVERCONFIG *sc)
{
    if(sc != NULL)
    {
        qDebug(logGui()) << "Accepting ServerConfig pointer into settings widget.";
        serverConfig = sc;
        haveServerConfig = true;
    }
}

void settingswidget::copyClusterList(QList<clusterSettings> c)
{
    this->clusters = c;
    haveClusterList = true;
    qDebug(logGui()) << "Updated settings widget clusters. Size of this->clusters: " << clusters.size();
    setUItoClustersList();
}

void settingswidget::insertClusterOutputText(QString text)
{
    ui->clusterOutputTextEdit->moveCursor(QTextCursor::End);
    ui->clusterOutputTextEdit->insertPlainText(text);
    ui->clusterOutputTextEdit->moveCursor(QTextCursor::End);
}

void settingswidget::setUItoClustersList()
{
    if(haveClusterList)
    {
        int defaultCluster = 0;
        int numClusters = clusters.size();
        if(numClusters > 0)
        {
            ui->clusterServerNameCombo->blockSignals(true);
            for (int f = 0; f < clusters.size(); f++)
            {
                ui->clusterServerNameCombo->addItem(clusters[f].server);
                if (clusters[f].isdefault) {
                    defaultCluster = f;
                }
            }
            ui->clusterServerNameCombo->blockSignals(false);
            if (clusters.size() > defaultCluster)
            {
                ui->clusterServerNameCombo->setCurrentIndex(defaultCluster);
                ui->clusterTcpPortLineEdit->blockSignals(true);
                ui->clusterUsernameLineEdit->blockSignals(true);
                ui->clusterPasswordLineEdit->blockSignals(true);
                ui->clusterTimeoutLineEdit->blockSignals(true);
                ui->clusterTcpPortLineEdit->setText(QString::number(clusters[defaultCluster].port));
                ui->clusterUsernameLineEdit->setText(clusters[defaultCluster].userName);
                ui->clusterPasswordLineEdit->setText(clusters[defaultCluster].password);
                ui->clusterTimeoutLineEdit->setText(QString::number(clusters[defaultCluster].timeout));
                ui->clusterTcpPortLineEdit->blockSignals(false);
                ui->clusterUsernameLineEdit->blockSignals(false);
                ui->clusterPasswordLineEdit->blockSignals(false);
                ui->clusterTimeoutLineEdit->blockSignals(false);
            }
        } else {
            // No clusters in the list
            ui->clusterTcpPortLineEdit->setEnabled(false);
            ui->clusterUsernameLineEdit->setEnabled(false);
            ui->clusterPasswordLineEdit->setEnabled(false);
            ui->clusterTimeoutLineEdit->setEnabled(false);
        }
    } else {
        qCritical(logGui()) << "Called to update UI of cluster info without cluister list.";
    }
}

void settingswidget::updateIfPrefs(quint64 items)
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
}

void settingswidget::updateColPrefs(quint64 items)
{
    prefColItem col;
    if(items & (int)if_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)col_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating Color pref" << (int)i;
            col = (prefColItem)i;
            updateColPref(col);
        }
    }
}

void settingswidget::updateRaPrefs(quint64 items)
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

void settingswidget::updateRsPrefs(quint64 items)
{
    prefRsItem prs;
    if(items & (int)rs_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)rs_all; i = i << 1)
    {
        if(items & i)
        {
            qInfo(logGui()) << "Updating Rs pref" << (int)i;
            prs = (prefRsItem)i;
            updateRsPref(prs);
        }
    }
}


void settingswidget::updateCtPrefs(quint64 items)
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

void settingswidget::updateServerConfigs(quint64 items)
{
    serverItems si;
    if(items & (int)s_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)s_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating ServerConfig" << (int)i;
            si = (serverItems)i;
            updateServerConfig(si);
        }
    }
}

void settingswidget::updateLanPrefs(quint64 items)
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

void settingswidget::updateClusterPrefs(quint64 items)
{
    prefClusterItem pcl;
    if(items & (int)cl_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)cl_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating Cluster pref" << (int)i;
            pcl = (prefClusterItem)i;
            updateClusterPref(pcl);
        }
    }
}

void settingswidget::updateIfPref(prefIfItem pif)
{
    if(prefs==NULL)
        return;

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
    case if_meter2Type:
    {
        ui->meter2selectionCombo->blockSignals(true);
        int m = ui->meter2selectionCombo->findData(prefs->meter2Type);
        ui->meter2selectionCombo->setCurrentIndex(m);
        ui->meter2selectionCombo->blockSignals(false);
        break;
    }
    case if_clickDragTuningEnable:
        quietlyUpdateCheckbox(ui->clickDragTuningEnableChk, prefs->clickDragTuningEnable);
        break;
    case if_currentColorPresetNumber:
        ui->colorPresetCombo->blockSignals(true);
        ui->colorPresetCombo->setCurrentIndex(prefs->currentColorPresetNumber);
        ui->colorPresetCombo->blockSignals(false);
        // activate? or done when prefs load? Maybe some of each?
        // TODO
        break;
    default:
        qWarning(logGui()) << "Did not understand if pref update item " << (int)pif;
        break;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateColPref(prefColItem col)
{
    if(prefs==NULL)
        return;

    updatingUIFromPrefs = true;
    switch(col)
    {
    case col_grid:
        break;
    default:
        qWarning(logGui()) << "Did not understand color pref update item " << (int)col;
        break;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateRaPref(prefRaItem pra)
{
    if(prefs==NULL)
        return;

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
        if(!haveSerialDevices)
        {
            qCritical(logGui()) << "Asked to show serial device without serial device list.";
            break;
        }
        if(!prefs->serialPortRadio.isEmpty())
        {
            int serialIndex = -1;
            if(prefs->serialPortRadio.toLower() == "auto")
            {
                serialIndex = ui->serialDeviceListCombo->findText(QString("Auto"));
            } else {
                serialIndex = ui->serialDeviceListCombo->findText(prefs->serialPortRadio);
            }
            if (serialIndex != -1) {
                ui->serialDeviceListCombo->setCurrentIndex(serialIndex);
            } else {
                qWarning(logGui()) << "Cannot find serial port" << prefs->serialPortRadio << "mentioned in preferences inside the serial combo box.";
            }
        } else {
            qDebug(logGui()) << "Serial port in prefs is blank";
        }
        break;
    }
    case ra_serialPortBaud:
        quietlyUpdateCombobox(ui->baudRateCombo,QVariant::fromValue(prefs->serialPortBaud));
        break;
    case ra_virtualSerialPort:
    {
        if(!haveVspDevices)
        {
            qCritical(logGui()) << "Asked to select VSP device without VSP device list.";
            break;
        }
        int vspIndex = ui->vspCombo->findText(prefs->virtualSerialPort);
        if (vspIndex != -1) {
            ui->vspCombo->setCurrentIndex(vspIndex);
        } else {
            // TODO: Are we sure this is a good idea?
            if(!prefs->virtualSerialPort.isEmpty())
            {
                ui->vspCombo->addItem(prefs->virtualSerialPort);
                ui->vspCombo->setCurrentIndex(ui->vspCombo->count() - 1);
            }
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

void settingswidget::updateRsPref(prefRsItem prs)
{
    if(prefs==NULL)
        return;

    updatingUIFromPrefs = true;
    switch(prs)
    {
    case rs_dataOffMod:
        quietlyUpdateCombobox(ui->modInputCombo,QVariant::fromValue(prefs->inputDataOff));
        break;
    case rs_data1Mod:
        quietlyUpdateCombobox(ui->modInputData1Combo,QVariant::fromValue(prefs->inputData1));
        break;
    case rs_data2Mod:
        quietlyUpdateCombobox(ui->modInputData2Combo,QVariant::fromValue(prefs->inputData2));
        break;
    case rs_data3Mod:
        quietlyUpdateCombobox(ui->modInputData3Combo,QVariant::fromValue(prefs->inputData3));
        break;
    default:
        qWarning(logGui()) << "Cannot update rs pref" << (int)prs;
    }
    updatingUIFromPrefs = false;
}



void settingswidget::updateCtPref(prefCtItem pct)
{
    if(prefs==NULL)
        return;

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
    if(prefs==NULL)
        return;

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
        qWarning(logGui()) << "Did not find matching preference for LAN ui update:" << (int)plan;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateClusterPref(prefClusterItem pcl)
{
    if(prefs==NULL)
        return;

    updatingUIFromPrefs = true;
    switch(pcl)
    {
    case cl_clusterUdpEnable:
        quietlyUpdateCheckbox(ui->clusterUdpEnable, prefs->clusterUdpEnable);
        break;
    case cl_clusterTcpEnable:
        quietlyUpdateCheckbox(ui->clusterTcpEnable, prefs->clusterTcpEnable);
        break;
    case cl_clusterUdpPort:
        ui->clusterUdpPortLineEdit->blockSignals(true);
        ui->clusterUdpPortLineEdit->setText(QString::number(prefs->clusterUdpPort));
        ui->clusterUdpPortLineEdit->blockSignals(false);
        break;
    case cl_clusterTcpServerName:
        // Take this from the clusters list
        break;
    case cl_clusterTcpUserName:
        // Take this from the clusters list
        break;
    case cl_clusterTcpPassword:
        // Take this from the clusters list
        break;
    case cl_clusterTcpPort:
        break;
    case cl_clusterTimeout:
        // Take this from the clusters list
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

void settingswidget::updateServerConfig(serverItems si)
{
    if(serverConfig == NULL)
    {
        qCritical(logGui()) << "serverConfig is NULL, cannot set preferences!";
        return;
    }
    updatingUIFromPrefs = true;
    switch(si)
    {
    case s_enabled:
        quietlyUpdateCheckbox(ui->serverEnableCheckbox, serverConfig->enabled);
        break;
    case s_lan:
        // Not used here
        break;
    case s_controlPort:
        quietlyUpdateLineEdit(ui->serverControlPortText, QString::number(serverConfig->civPort));
        break;
    case s_civPort:
        quietlyUpdateLineEdit(ui->serverCivPortText, QString::number(serverConfig->civPort));
        break;
    case s_audioPort:
        quietlyUpdateLineEdit(ui->serverAudioPortText, QString::number(serverConfig->audioPort));
        break;
    case s_audioOutput:
        break;
    case s_audioInput:
        break;
    case s_resampleQuality:
        // Not used here
        break;
    case s_baudRate:
        // Not used here
        break;
    case s_users:
        // This is fairly complex
        populateServerUsers();
        break;
    case s_rigs:
        // Not used here
        break;
    default:
        qWarning(logGui()) << "Did not find matching UI element for ServerConfig " << (int)si;
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
    for(int i=1; i < (int)u_all; i = i << 1)
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
        ui->ipAddressTxt->blockSignals(true);
        ui->ipAddressTxt->setEnabled(ui->lanEnableBtn->isChecked());
        ui->ipAddressTxt->setText(udpPrefs->ipAddress);
        ui->ipAddressTxt->blockSignals(false);
        break;
    case u_controlLANPort:
        ui->controlPortTxt->blockSignals(true);
        ui->controlPortTxt->setEnabled(ui->lanEnableBtn->isChecked());
        ui->controlPortTxt->setText(QString("%1").arg(udpPrefs->controlLANPort));
        ui->controlPortTxt->blockSignals(false);
        break;
    case u_serialLANPort:
        // Not used in the UI.
        break;
    case u_audioLANPort:
        // Not used in the UI.
        break;
    case u_username:
        ui->usernameTxt->blockSignals(true);
        ui->usernameTxt->setEnabled(ui->lanEnableBtn->isChecked());
        ui->usernameTxt->setText(QString("%1").arg(udpPrefs->username));
        ui->usernameTxt->blockSignals(false);
        break;
    case u_password:
        ui->passwordTxt->blockSignals(true);
        ui->passwordTxt->setEnabled(ui->lanEnableBtn->isChecked());
        ui->passwordTxt->setText(QString("%1").arg(udpPrefs->password));
        ui->passwordTxt->blockSignals(false);
        break;
    case u_clientName:
        // Not used in the UI.
        break;
    case u_waterfallFormat:
        ui->waterfallFormatCombo->blockSignals(true);
        ui->waterfallFormatCombo->setCurrentIndex(prefs->waterfallFormat);
        ui->waterfallFormatCombo->blockSignals(false);
        break;
    case u_halfDuplex:
        ui->audioDuplexCombo->blockSignals(true);
        ui->audioDuplexCombo->setCurrentIndex((int)udpPrefs->halfDuplex);
        ui->audioDuplexCombo->blockSignals(false);
        break;
    default:
        qWarning(logGui()) << "Did not find matching UI element for UDP pref item " << (int)upi;
        break;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateAllPrefs()
{
    // DEPRECIATED
    // Maybe make this a public convenience function

    // Review all the preferences. This is intended to be called
    // after new settings are loaded in.
    // Not sure if we actually want to always assume we should load prefs.
    updatingUIFromPrefs = true;
    if(havePrefs)
    {
        updateIfPrefs((int)if_all);
        updateRaPrefs((int)ra_all);
        updateCtPrefs((int)ct_all);
        updateClusterPrefs((int)cl_all);
        updateLanPrefs((int)l_all);
    }
    if(haveUdpPrefs)
    {
        updateUdpPrefs((int)u_all);
    }

    updatingUIFromPrefs = false;
}

void settingswidget::updateAudioInputs(QStringList deviceList, int currentIndex, int chars)
{
    // see wfmain::setAudioDevicesUI()
    if(haveAudioInputs)
        qDebug(logGui()) << "Reloading audio input list";
    ui->audioInputCombo->blockSignals(true);
    ui->audioInputCombo->clear();
    ui->audioInputCombo->addItems(deviceList);
    ui->audioInputCombo->setCurrentIndex(-1);
    ui->audioInputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(chars + 30));
    ui->audioInputCombo->blockSignals(false);
    ui->audioInputCombo->setCurrentIndex(currentIndex);
    haveAudioInputs = true;
}

void settingswidget::updateAudioOutputs(QStringList deviceList, int currentIndex, int chars)
{
    if(haveAudioOutputs)
        qDebug(logGui()) << "Reloading audio output list";
    ui->audioOutputCombo->blockSignals(true);
    ui->audioOutputCombo->clear();
    ui->audioOutputCombo->addItems(deviceList);
    ui->audioOutputCombo->setCurrentIndex(-1);
    ui->audioOutputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(chars + 30));
    ui->audioOutputCombo->blockSignals(false);
    ui->audioOutputCombo->setCurrentIndex(currentIndex);
    haveAudioOutputs = true;
}

void settingswidget::updateServerTXAudioOutputs(QStringList deviceList, int currentIndex, int chars)
{
    if(haveServerAudioOutputs)
        qDebug(logGui()) << "Reloading ServerTXAudioOutputs";
    ui->serverTXAudioOutputCombo->blockSignals(true);
    ui->serverTXAudioOutputCombo->clear();
    ui->serverTXAudioOutputCombo->addItems(deviceList);
    ui->serverTXAudioOutputCombo->setCurrentIndex(-1);
    ui->serverTXAudioOutputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(chars + 30));
    ui->serverTXAudioOutputCombo->setCurrentIndex(currentIndex);
    ui->serverTXAudioOutputCombo->blockSignals(false);
    haveServerAudioOutputs = true;
}

void settingswidget::updateServerRXAudioInputs(QStringList deviceList, int currentIndex, int chars)
{
    if(haveServerAudioInputs)
        qDebug(logGui()) << "Reloading ServerRXAudioInputs";
    ui->serverRXAudioInputCombo->blockSignals(true);
    ui->serverRXAudioInputCombo->clear();
    ui->serverRXAudioInputCombo->addItems(deviceList);
    ui->serverRXAudioInputCombo->setCurrentIndex(-1);
    ui->serverRXAudioInputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(chars+30));
    ui->serverRXAudioInputCombo->setCurrentIndex(currentIndex);
    ui->serverRXAudioInputCombo->blockSignals(false);
    haveServerAudioInputs = true;
}

void settingswidget::updateSerialPortList(QStringList deviceList, QVector<int> data)
{
    if(deviceList.length() == data.length())
    {
        ui->serialDeviceListCombo->blockSignals(true);
        ui->serialDeviceListCombo->addItem("Auto", 0);
        for(int i=0; i < deviceList.length(); i++)
        {
            ui->serialDeviceListCombo->addItem(deviceList.at(i), data.at(i));
        }
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC) || defined(Q_OS_UNIX)
        ui->serialDeviceListCombo->addItem("Manual...", 256);
#endif
        ui->serialDeviceListCombo->blockSignals(false);
        haveSerialDevices = true;
    } else {
        qCritical(logGui()) << "Cannot populate serial device list. Data of unequal length.";
    }
}

void settingswidget::updateVSPList(QStringList deviceList, QVector<int> data)
{
    // Do not supply "None" device, that is a UI thing and it is done here.
    // Supply the complete filename

    // TODO: Should we clear the list first?
    if(deviceList.length() == data.length())
    {
        ui->vspCombo->blockSignals(true);

        for(int i=0; i < deviceList.length(); i++)
        {
            ui->vspCombo->addItem(deviceList.at(i), data.at(i));

            if (QFile::exists(deviceList.at(i))) {
                auto* model = qobject_cast<QStandardItemModel*>(ui->vspCombo->model());
                auto* item = model->item(ui->vspCombo->count() - 1);
                item->setEnabled(false);
            }
        }

        ui->serialDeviceListCombo->addItem("None", deviceList.size());

        ui->vspCombo->blockSignals(false);
        haveVspDevices = true;
    } else {
        qCritical(logGui()) << "Cannot populate serial device list. Data of unequal length.";
    }
}

void settingswidget::updateModSourceList(uchar num, QVector<rigInput> data)
{

    QComboBox* combo;
    switch (num)
    {
    case 0:
        combo = ui->modInputCombo;
        break;
    case 1:
        combo = ui->modInputData1Combo;
        break;
    case 2:
        combo = ui->modInputData2Combo;
        ui->modInputData2ComboText->setVisible(true);
        break;
    case 3:
        combo = ui->modInputData3Combo;
        ui->modInputData3ComboText->setVisible(true);
        break;
    default:
        return;
    }

    combo->blockSignals(true);
    combo->clear();

    foreach (auto input, data)
    {
        combo->addItem(input.name, QVariant::fromValue(input.type));
    }

    if (data.length()==0){
        combo->addItem("None", QVariant::fromValue(inputNone));
    }

    combo->setVisible(true);
    combo->blockSignals(false);
}

void settingswidget::populateServerUsers()
{
    // Copy data from serverConfig.users into the server UI table
    // We will assume the data are safe to use.
    bool blank = false;
    int row=0;
    qDebug(logGui()) << "Adding server users. Size: " << serverConfig->users.size();

    QList<SERVERUSER>::iterator user = serverConfig->users.begin();
    while (user != serverConfig->users.end())
    {
        serverAddUserLine(user->username, user->password, user->userType);
        if((user->username == "") && !blank)
            blank = true;
        row++;
        user++;
    }
    if (row==1 && blank)
    {
        // There are no defined users. The only user present is the blank one.
        // The button is disabled, but it may be enabled
        // when the user enters valid information for a potential new
        // user account.
        ui->serverAddUserBtn->setEnabled(false);
    }
}

void settingswidget::serverAddUserLine(const QString &user, const QString &pass, const int &type)
{
    // migration TODO: Review these signals/slots

    ui->serverUsersTable->blockSignals(true);

    ui->serverUsersTable->insertRow(ui->serverUsersTable->rowCount());

    ui->serverUsersTable->setItem(ui->serverUsersTable->rowCount() - 1, 0, new QTableWidgetItem());
    ui->serverUsersTable->setItem(ui->serverUsersTable->rowCount() - 1, 1, new QTableWidgetItem());
    ui->serverUsersTable->setItem(ui->serverUsersTable->rowCount() - 1, 2, new QTableWidgetItem());
    ui->serverUsersTable->setItem(ui->serverUsersTable->rowCount() - 1, 3, new QTableWidgetItem());

    QLineEdit* username = new QLineEdit();
    username->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    username->setProperty("col", (int)0);
    username->setText(user);
    connect(username, SIGNAL(editingFinished()), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 0, username);

    QLineEdit* password = new QLineEdit();
    password->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    password->setProperty("col", (int)1);
    password->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    password->setText(pass);
    connect(password, SIGNAL(editingFinished()), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 1, password);

    QComboBox* comboBox = new QComboBox();
    comboBox->insertItems(0, { "Full User","Full with no TX","Monitor only" });
    comboBox->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    comboBox->setProperty("col", (int)2);
    comboBox->setCurrentIndex(type);
    connect(comboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 2, comboBox);

    QPushButton* button = new QPushButton();
    button->setText("Delete");
    button->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    button->setProperty("col", (int)3);
    connect(button, SIGNAL(clicked()), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 3, button);

    ui->serverUsersTable->blockSignals(false);
}

// Utility Functions:
void settingswidget::updateUnderlayMode()
{

    quietlyUpdateRadiobutton(ui->underlayNone, false);
    quietlyUpdateRadiobutton(ui->underlayPeakHold, false);
    quietlyUpdateRadiobutton(ui->underlayPeakBuffer, false);
    quietlyUpdateRadiobutton(ui->underlayAverageBuffer, false);

    switch(prefs->underlayMode) {
    case underlayNone:
        quietlyUpdateRadiobutton(ui->underlayNone, true);
        ui->underlayBufferSlider->setDisabled(true);
        break;
    case underlayPeakHold:
        quietlyUpdateRadiobutton(ui->underlayPeakHold, true);
        ui->underlayBufferSlider->setDisabled(true);
        break;
    case underlayPeakBuffer:
        quietlyUpdateRadiobutton(ui->underlayPeakBuffer, true);
        ui->underlayBufferSlider->setEnabled(true);
        break;
    case underlayAverageBuffer:
        quietlyUpdateRadiobutton(ui->underlayAverageBuffer, true);
        ui->underlayBufferSlider->setEnabled(true);
        break;
    default:
        qWarning() << "Do not understand underlay mode: " << (unsigned int) prefs->underlayMode;
    }
}

void settingswidget::quietlyUpdateSlider(QSlider *sl, int val)
{
    sl->blockSignals(true);
    if( (val >= sl->minimum()) && (val <= sl->maximum()) )
        sl->setValue(val);
    sl->blockSignals(false);
}

void settingswidget::quietlyUpdateCombobox(QComboBox *cb, int index)
{
    cb->blockSignals(true);
    cb->setCurrentIndex(index);
    cb->blockSignals(false);
}

void settingswidget::quietlyUpdateCombobox(QComboBox *cb, QVariant val)
{
    cb->blockSignals(true);
    cb->setCurrentIndex(cb->findData(val));
    cb->blockSignals(false);
}

void settingswidget::quietlyUpdateSpinbox(QSpinBox *sb, int val)
{
    sb->blockSignals(true);
    if( (val >= sb->minimum()) && (val <= sb->maximum()) )
        sb->setValue(val);
    sb->blockSignals(false);
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

void settingswidget::quietlyUpdateLineEdit(QLineEdit *le, const QString text)
{
    le->blockSignals(true);
    le->setText(text);
    le->blockSignals(false);
}

// Resulting from User Interaction

void settingswidget::on_debugBtn_clicked()
{
    qDebug(logGui()) << "Debug button pressed in settings widget.";
}

void settingswidget::on_settingsList_currentRowChanged(int currentRow)
{
    ui->settingsStack->setCurrentIndex(currentRow);
}

void settingswidget::onServerUserFieldChanged()
{
    if(!haveServerConfig) {
        qCritical(logGui()) << "Do not have serverConfig, cannot edit users.";
        return;
    }
    int row = sender()->property("row").toInt();
    int col = sender()->property("col").toInt();
    qDebug() << "Server User field col" << col << "row" << row << "changed";

    // This is a new user line so add to serverUsersTable
    if (serverConfig->users.length() <= row)
    {
        qInfo() << "Something bad has happened, serverConfig.users is shorter than table!";
    }
    else
    {
        if (col == 0)
        {
            QLineEdit* username = (QLineEdit*)ui->serverUsersTable->cellWidget(row, 0);
            if (username->text() != serverConfig->users[row].username) {
                serverConfig->users[row].username = username->text();
            }
        }
        else if (col == 1)
        {
            QLineEdit* password = (QLineEdit*)ui->serverUsersTable->cellWidget(row, 1);
            QByteArray pass;
            passcode(password->text(), pass);
            if (QString(pass) != serverConfig->users[row].password) {
                serverConfig->users[row].password = pass;
            }
        }
        else if (col == 2)
        {
            QComboBox* comboBox = (QComboBox*)ui->serverUsersTable->cellWidget(row, 2);
            serverConfig->users[row].userType = comboBox->currentIndex();
        }
        else if (col == 3)
        {
            serverConfig->users.removeAt(row);
            ui->serverUsersTable->setRowCount(0);
            foreach(SERVERUSER user, serverConfig->users)
            {
                serverAddUserLine(user.username, user.password, user.userType);
            }
        }
        if (row == ui->serverUsersTable->rowCount() - 1) {
            ui->serverAddUserBtn->setEnabled(true);
        }

    }
}

void settingswidget::on_lanEnableBtn_clicked(bool checked)
{
    // TODO: prefs.enableLAN = checked;
    // TOTO? ui->connectBtn->setEnabled(true); // move to other side
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
    ui->useRTSforPTTchk->setEnabled(!checked);

    prefs->enableLAN = checked;
    emit changedLanPref(l_enableLAN);
}

void settingswidget::on_serialEnableBtn_clicked(bool checked)
{
    prefs->enableLAN = !checked;
    ui->serialDeviceListCombo->setEnabled(checked);

    ui->ipAddressTxt->setEnabled(!checked);
    ui->controlPortTxt->setEnabled(!checked);
    ui->usernameTxt->setEnabled(!checked);
    ui->passwordTxt->setEnabled(!checked);
    ui->audioRXCodecCombo->setEnabled(!checked);
    ui->audioTXCodecCombo->setEnabled(!checked);
    ui->audioSampleRateCombo->setEnabled(!checked);
    ui->rxLatencySlider->setEnabled(!checked);
    ui->txLatencySlider->setEnabled(!checked);
    ui->rxLatencyValue->setEnabled(!checked);
    ui->txLatencyValue->setEnabled(!checked);
    ui->audioOutputCombo->setEnabled(!checked);
    ui->audioInputCombo->setEnabled(!checked);
    ui->baudRateCombo->setEnabled(checked);
    ui->serialDeviceListCombo->setEnabled(checked);
    ui->serverRXAudioInputCombo->setEnabled(checked);
    ui->serverTXAudioOutputCombo->setEnabled(checked);
    ui->useRTSforPTTchk->setEnabled(checked);

    emit changedLanPref(l_enableLAN);
}

void settingswidget::on_autoSSBchk_clicked(bool checked)
{
    prefs->automaticSidebandSwitching = checked;
    emit changedCtPref(ct_automaticSidebandSwitching);
}

void settingswidget::on_useSystemThemeChk_clicked(bool checked)
{
    prefs->useSystemTheme = checked;
    emit changedIfPrefs(if_useSystemTheme);
}

void settingswidget::on_enableUsbChk_clicked(bool checked)
{
    prefs->enableUSBControllers = checked;
    ui->usbControllerBtn->setEnabled(checked);
    ui->usbButtonsResetBtn->setEnabled(checked);
    ui->usbCommandsResetBtn->setEnabled(checked);
    ui->usbResetLbl->setVisible(checked);

    emit changedCtPref(ct_enableUSBControllers);
}

void settingswidget::on_usbControllerBtn_clicked()
{
    emit showUSBControllerSetup();
}

void settingswidget::on_autoPollBtn_clicked(bool checked)
{
    if(checked)
    {
        prefs->polling_ms = 0;
        emit changedRaPref(ra_polling_ms);
        ui->pollTimeMsSpin->setEnabled(false);
    }
}

void settingswidget::on_manualPollBtn_clicked(bool checked)
{
    if(checked)
    {
        ui->pollTimeMsSpin->setEnabled(true);
        prefs->polling_ms = ui->pollTimeMsSpin->value();
        emit changedRaPref(ra_polling_ms);
    } else {
        // Do we need this...
        //        prefs.polling_ms = 0;
        //        emit changedRaPref(ra_polling_ms);
    }
}

void settingswidget::on_pollTimeMsSpin_valueChanged(int val)
{
    prefs->polling_ms = val;
    emit changedRaPref(ra_polling_ms);
}

void settingswidget::on_serialDeviceListCombo_activated(const QString &arg1)
{
    QString manualPort;
    bool ok;
    if(arg1==QString("Manual..."))
    {
        manualPort = QInputDialog::getText(this, tr("Manual port assignment"),
                                           tr("Enter serial port assignment:"),
                                           QLineEdit::Normal,
                                           tr("/dev/device"), &ok);
        if(manualPort.isEmpty() || !ok)
        {
            quietlyUpdateCombobox(ui->serialDeviceListCombo,0);
            return;
        } else {
            prefs->serialPortRadio = manualPort;
            qInfo(logGui()) << "Setting preferences to use manually-assigned serial port: " << manualPort;
            ui->serialEnableBtn->setChecked(true);
            emit changedRaPref(ra_serialPortRadio);
            return;
        }
    }
    if(arg1==QString("Auto"))
    {
        prefs->serialPortRadio = "auto";
        qInfo(logGui()) << "Setting preferences to automatically find rig serial port.";
        ui->serialEnableBtn->setChecked(true);
        emit changedRaPref(ra_serialPortRadio);
        return;
    }

    prefs->serialPortRadio = arg1;
    qInfo(logGui()) << "Setting preferences to use manually-assigned serial port: " << arg1;
    ui->serialEnableBtn->setChecked(true);
    emit changedRaPref(ra_serialPortRadio);
}

void settingswidget::on_baudRateCombo_activated(int index)
{
    bool ok = false;
    quint32 baud = ui->baudRateCombo->currentData().toUInt(&ok);
    if(ok)
    {
        prefs->serialPortBaud = baud;
        qInfo(logGui()) << "Changed baud rate to" << baud << "bps. Press Save Settings to retain.";
        emit changedRaPref(ra_serialPortBaud);
    }
    (void)index;
}

void settingswidget::on_vspCombo_activated(int index)
{
    Q_UNUSED(index);
    prefs->virtualSerialPort = ui->vspCombo->currentText();
    emit changedRaPref(ra_virtualSerialPort);
}

void settingswidget::on_audioSystemCombo_currentIndexChanged(int value)
{
    prefs->audioSystem = static_cast<audioType>(value);
    quietlyUpdateCombobox(ui->audioSystemServerCombo,value);
    emit changedRaPref(ra_audioSystem);
}

void settingswidget::on_audioSystemServerCombo_currentIndexChanged(int value)
{
    prefs->audioSystem = static_cast<audioType>(value);
    quietlyUpdateCombobox(ui->audioSystemCombo,value);
    emit changedRaPref(ra_audioSystem);
}

void settingswidget::on_meter2selectionCombo_currentIndexChanged(int index)
{
    prefs->meter2Type = static_cast<meter_t>(ui->meter2selectionCombo->itemData(index).toInt());
    emit changedIfPref(if_meter2Type);
}

void settingswidget::on_tuningFloorZerosChk_clicked(bool checked)
{
    prefs->niceTS = checked;
    emit changedCtPref(ct_niceTS);
}

void settingswidget::on_fullScreenChk_clicked(bool checked)
{
    prefs->useFullScreen = checked;
    emit changedIfPref(if_useFullScreen);
}

void settingswidget::on_wfAntiAliasChk_clicked(bool checked)
{
    prefs->wfAntiAlias = checked;
    emit changedIfPref(if_wfAntiAlias);
}

void settingswidget::on_wfInterpolateChk_clicked(bool checked)
{
    prefs->wfInterpolate = checked;
    emit changedIfPref(if_wfInterpolate);
}

void settingswidget::on_underlayNone_clicked(bool checked)
{
    if(checked)
    {
        prefs->underlayMode = underlayNone;
        ui->underlayBufferSlider->setDisabled(true);
        emit changedIfPref(if_underlayMode);
    }
}

void settingswidget::on_underlayPeakHold_clicked(bool checked)
{
    if(checked)
    {
        prefs->underlayMode = underlayPeakHold;
        ui->underlayBufferSlider->setDisabled(true);
        emit changedIfPref(if_underlayMode);
    }
}

void settingswidget::on_underlayPeakBuffer_clicked(bool checked)
{
    if(checked)
    {
        prefs->underlayMode = underlayPeakBuffer;
        ui->underlayBufferSlider->setEnabled(true);
        emit changedIfPref(if_underlayMode);
    }
}

void settingswidget::on_underlayAverageBuffer_clicked(bool checked)
{
    if(checked)
    {
        prefs->underlayMode = underlayAverageBuffer;
        ui->underlayBufferSlider->setEnabled(true);
        emit changedIfPref(if_underlayMode);
    }
}

void settingswidget::on_underlayBufferSlider_valueChanged(int value)
{
    prefs->underlayBufferSize = value;
    emit changedIfPref(if_underlayBufferSize);
}

void settingswidget::on_pttEnableChk_clicked(bool checked)
{
    prefs->enablePTT = checked;
    emit changedCtPref(ct_enablePTT);
}

void settingswidget::on_clickDragTuningEnableChk_clicked(bool checked)
{
    prefs->clickDragTuningEnable = checked;
    emit changedIfPref(if_clickDragTuningEnable);
}

void settingswidget::on_enableRigctldChk_clicked(bool checked)
{
    prefs->enableRigCtlD = checked;
    emit changedLanPref(l_enableRigCtlD);
}

void settingswidget::on_rigctldPortTxt_editingFinished()
{
    bool okconvert = false;
    unsigned int port = ui->rigctldPortTxt->text().toUInt(&okconvert);
    if (okconvert)
    {
        prefs->rigCtlPort = port;
        emit changedLanPref(l_rigCtlPort);
    }
}

void settingswidget::on_tcpServerPortTxt_editingFinished()
{
    bool okconvert = false;
    unsigned int port = ui->tcpServerPortTxt->text().toUInt(&okconvert);
    if (okconvert)
    {
        prefs->tcpPort = port;
        emit changedLanPref(l_tcpPort);
    }
}

void settingswidget::on_clusterServerNameCombo_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    if(!haveClusterList)
        qWarning(logGui()) << "Do not have cluster list yet. Editing may not work.";

    QString text = ui->clusterServerNameCombo->currentText();

    if (clusters.size() <= index)
    {
        qInfo(logGui) << "Adding Cluster server" << text;
        clusterSettings c;
        c.server = text;
        clusters.append(c);
        ui->clusterTcpPortLineEdit->setEnabled(true);
        ui->clusterUsernameLineEdit->setEnabled(true);
        ui->clusterPasswordLineEdit->setEnabled(true);
        ui->clusterTimeoutLineEdit->setEnabled(true);

    }
    else {
        qInfo(logGui) << "Editing Cluster server" << text;
        clusters[index].server = text;
    }
    quietlyUpdateLineEdit(ui->clusterTcpPortLineEdit,QString::number(clusters[index].port));
    quietlyUpdateLineEdit(ui->clusterUsernameLineEdit,clusters[index].userName);
    quietlyUpdateLineEdit(ui->clusterPasswordLineEdit,clusters[index].password);
    quietlyUpdateLineEdit(ui->clusterTimeoutLineEdit,QString::number(clusters[index].timeout));

    for (int i = 0; i < clusters.size(); i++) {
        if (i == index)
            clusters[i].isdefault = true;
        else
            clusters[i].isdefault = false;
    }

    // For reference, here is what the code on the other side is basically doing:
//    emit setClusterServerName(clusters[index].server);
//    emit setClusterTcpPort(clusters[index].port);
//    emit setClusterUserName(clusters[index].userName);
//    emit setClusterPassword(clusters[index].password);
//    emit setClusterTimeout(clusters[index].timeout);

    // We are using the not-arrayed data in preferences
    // to communicate the current cluster information:
    prefs->clusterTcpServerName = clusters[index].server;
    prefs->clusterTcpPort = clusters[index].port;
    prefs->clusterTcpUserName = clusters[index].userName;
    prefs->clusterTcpPassword = clusters[index].password;
    prefs->clusterTimeout = clusters[index].timeout;
    prefs->clusterTcpEnable = ui->clusterTcpEnable->isChecked();

    emit changedClusterPrefs(cl_clusterTcpServerName |
                             cl_clusterTcpPort |
                             cl_clusterTcpUserName |
                             cl_clusterTcpPassword |
                             cl_clusterTimeout |
                             cl_clusterTcpEnable);
}

void settingswidget::on_clusterUdpEnable_clicked(bool checked)
{
    prefs->clusterUdpEnable = checked;
    emit changedClusterPref(cl_clusterUdpEnable);
}

void settingswidget::on_clusterTcpEnable_clicked(bool checked)
{
    prefs->clusterTcpEnable = checked;
    emit changedClusterPref(cl_clusterTcpEnable);
}

void settingswidget::on_clusterTcpSetNowBtn_clicked()
{
    // Maybe this will be easier than "hit enter" ?
    int index = ui->clusterServerNameCombo->currentIndex();
    on_clusterServerNameCombo_currentIndexChanged(index);
}

void settingswidget::on_clusterServerNameCombo_currentTextChanged(const QString &text)
{
    if (text.isEmpty()) {
        int index = ui->clusterServerNameCombo->currentIndex();
        ui->clusterServerNameCombo->removeItem(index);
        clusters.removeAt(index);
        if (clusters.size() == 0)
        {
            ui->clusterTcpPortLineEdit->setEnabled(false);
            ui->clusterUsernameLineEdit->setEnabled(false);
            ui->clusterPasswordLineEdit->setEnabled(false);
            ui->clusterTimeoutLineEdit->setEnabled(false);
        }
    }
}

void settingswidget::on_clusterTcpPortLineEdit_editingFinished()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < clusters.size())
    {
        clusters[index].port = ui->clusterTcpPortLineEdit->displayText().toInt();
        //emit setClusterTcpPort(clusters[index].port);
        prefs->clusterTcpPort = clusters[index].port;
        emit changedClusterPref(cl_clusterTcpPort);
    }
}

void settingswidget::on_clusterUsernameLineEdit_editingFinished()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < clusters.size())
    {
        clusters[index].userName = ui->clusterUsernameLineEdit->text();
        //emit setClusterUserName(clusters[index].userName);
        prefs->clusterTcpUserName = clusters[index].userName;
        emit changedClusterPref(cl_clusterTcpUserName);
    }
}

void settingswidget::on_clusterPasswordLineEdit_editingFinished()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < clusters.size())
    {
        clusters[index].password = ui->clusterPasswordLineEdit->text();
        //emit setClusterPassword(clusters[index].password);
        prefs->clusterTcpPassword = clusters[index].password;
        emit changedClusterPref(cl_clusterTcpPassword);
    }
}

void settingswidget::on_clusterTimeoutLineEdit_editingFinished()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < clusters.size())
    {
        clusters[index].timeout = ui->clusterTimeoutLineEdit->displayText().toInt();
        //emit setClusterTimeout(clusters[index].timeout);
        prefs->clusterTimeout = clusters[index].timeout;
        emit changedClusterPref(cl_clusterTimeout);
    }
}

void settingswidget::on_clusterUdpPortLineEdit_editingFinished()
{
    bool ok = false;
    int p = ui->clusterUdpPortLineEdit->text().toInt(&ok);
    if(ok)
    {
        prefs->clusterUdpPort = p;
        emit changedClusterPref(cl_clusterUdpPort);
    }
}

void settingswidget::on_clusterSkimmerSpotsEnable_clicked(bool checked)
{
    prefs->clusterSkimmerSpotsEnable = checked;
    emit changedClusterPref(cl_clusterSkimmerSpotsEnable);
}

void settingswidget::on_ipAddressTxt_textChanged(const QString &arg1)
{
    udpPrefs->ipAddress = arg1;
    emit changedUdpPref(u_ipAddress);
}

void settingswidget::on_usernameTxt_textChanged(const QString &arg1)
{
    udpPrefs->username = arg1;
    emit changedUdpPref(u_username);
}

void settingswidget::on_controlPortTxt_textChanged(const QString &arg1)
{
    bool ok = false;
    int port = arg1.toUInt(&ok);
    if(ok)
    {
        udpPrefs->controlLANPort = port;
        emit changedUdpPref(u_controlLANPort);
    }
}

void settingswidget::on_passwordTxt_textChanged(const QString &arg1)
{
    udpPrefs->password = arg1;
    emit changedUdpPref(u_password);
}

void settingswidget::on_audioDuplexCombo_currentIndexChanged(int index)
{
    udpPrefs->halfDuplex = (bool)index;
    emit changedUdpPref(u_halfDuplex);
}

void settingswidget::on_audioOutputCombo_currentIndexChanged(int index)
{
    emit changedAudioOutputCombo(index);
}

void settingswidget::on_audioInputCombo_currentIndexChanged(int index)
{
    emit changedAudioInputCombo(index);
}

void settingswidget::on_serverRXAudioInputCombo_currentIndexChanged(int index)
{
    emit changedServerRXAudioInputCombo(index);
}

void settingswidget::on_serverTXAudioOutputCombo_currentIndexChanged(int index)
{
    emit changedServerTXAudioOutputCombo(index);
}

void settingswidget::on_serverEnableCheckbox_clicked(bool checked)
{
    serverConfig->enabled = checked;
    emit changedServerConfig(s_enabled);
}

void settingswidget::on_serverAddUserBtn_clicked()
{
    if(!haveServerConfig)
    {
        qCritical(logGui()) << "Cannot modify users without valid serverConfig.";
        return;
    }
    serverAddUserLine("", "", 0);
    SERVERUSER user;
    user.username = "";
    user.password = "";
    user.userType = 0;
    serverConfig->users.append(user);

    ui->serverAddUserBtn->setEnabled(false);
}


void settingswidget::on_modInputCombo_activated(int index)
{
    emit changedModInput(0,ui->modInputCombo->currentData().value<inputTypes>());
}

void settingswidget::on_modInputData1Combo_activated(int index)
{
    emit changedModInput(1,ui->modInputData1Combo->currentData().value<inputTypes>());
}


void settingswidget::on_modInputData2Combo_activated(int index)
{
    emit changedModInput(2,ui->modInputData2Combo->currentData().value<inputTypes>());
}


void settingswidget::on_modInputData3Combo_activated(int index)
{
    emit changedModInput(3,ui->modInputData3Combo->currentData().value<inputTypes>());
}
