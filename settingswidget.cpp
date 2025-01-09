#include "settingswidget.h"
#include "qserialportinfo.h"
#include "ui_settingswidget.h"

#define setchk(a,b) quietlyUpdateCheckbox(a,b)

settingswidget::settingswidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::settingswidget)
{
    ui->setupUi(this);

    connect(ui->serverUsersTable,SIGNAL(rowAdded(int)),this, SLOT(serverAddUserLine(int)));
    createSettingsListItems();
    populateComboBoxes();

#ifdef QT_DEBUG
    ui->debugBtn->setVisible(true);
    ui->satOpsBtn->setVisible(true);
#else
    ui->debugBtn->setVisible(false);
    ui->satOpsBtn->setVisible(false);
#endif

}

settingswidget::~settingswidget()
{
    delete ui;

    if (audioDev != Q_NULLPTR) {
        delete audioDev;
    }

    if (prefs->audioSystem == portAudio) {
        Pa_Terminate();
    }
}

// Startup:
void settingswidget::createSettingsListItems()
{
    // Add items to the settings tab list widget
    ui->settingsList->addItem(tr("Radio Access"));     // 0
    ui->settingsList->addItem(tr("User Interface"));   // 1
    ui->settingsList->addItem(tr("Radio Settings"));   // 2
    ui->settingsList->addItem(tr("Radio Server"));     // 3
    ui->settingsList->addItem(tr("External Control")); // 4
    ui->settingsList->addItem(tr("DX Cluster"));       // 5
    ui->settingsList->addItem(tr("Experimental"));     // 6
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

    ui->meter3selectionCombo->blockSignals(true);
    ui->meter3selectionCombo->addItem("None", meterNone);
    ui->meter3selectionCombo->addItem("SWR", meterSWR);
    ui->meter3selectionCombo->addItem("ALC", meterALC);
    ui->meter3selectionCombo->addItem("Compression", meterComp);
    ui->meter3selectionCombo->addItem("Voltage", meterVoltage);
    ui->meter3selectionCombo->addItem("Current", meterCurrent);
    ui->meter3selectionCombo->addItem("Center", meterCenter);
    ui->meter3selectionCombo->addItem("TxRxAudio", meterAudio);
    ui->meter3selectionCombo->addItem("RxAudio", meterRxAudio);
    ui->meter3selectionCombo->addItem("TxAudio", meterTxMod);
    ui->meter3selectionCombo->show();
    ui->meter3selectionCombo->blockSignals(false);

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

    // Set color preset combo to an invalid value to force the current preset to be loaded
    ui->colorPresetCombo->blockSignals(true);
    ui->colorPresetCombo->setCurrentIndex(-1);
    ui->colorPresetCombo->blockSignals(false);

    ui->decimalSeparatorsCombo->blockSignals(true);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    ui->decimalSeparatorsCombo->addItem(QString("\"%0\"").arg(QLocale().decimalPoint()),QLocale().decimalPoint());
#else
    ui->decimalSeparatorsCombo->addItem(QString("\"%0\"").arg(QLocale().decimalPoint()),QLocale().decimalPoint().at(0));
#endif
    ui->decimalSeparatorsCombo->addItem("\",\"",QChar(','));
    ui->decimalSeparatorsCombo->addItem("\".\"",QChar('.'));
    ui->decimalSeparatorsCombo->addItem("\":\"",QChar(':'));
    ui->decimalSeparatorsCombo->addItem("\" \"",QChar(' '));
    ui->decimalSeparatorsCombo->setCurrentIndex(0);
    ui->decimalSeparatorsCombo->blockSignals(false);

    ui->groupSeparatorsCombo->blockSignals(true);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    ui->groupSeparatorsCombo->addItem(QString("\"%0\"").arg(QLocale().groupSeparator()),QLocale().groupSeparator());
#else
    ui->groupSeparatorsCombo->addItem(QString("\"%0\"").arg(QLocale().groupSeparator()),QLocale().groupSeparator().at(0));
#endif
    ui->groupSeparatorsCombo->addItem("\",\"",QChar(','));
    ui->groupSeparatorsCombo->addItem("\".\"",QChar('.'));
    ui->groupSeparatorsCombo->addItem("\":\"",QChar(':'));
    ui->groupSeparatorsCombo->addItem("\" \"",QChar(' '));
    ui->groupSeparatorsCombo->setCurrentIndex(0);
    ui->groupSeparatorsCombo->blockSignals(false);

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
    // New we have prefs, we get audio information

    if (audioDev == Q_NULLPTR) {
        audioDev = new audioDevices(prefs->audioSystem, QFontMetrics(ui->audioSystemCombo->font()));
        connect(audioDev, SIGNAL(updated()), this, SLOT(setAudioDevicesUI()));
        audioDev->enumerate();
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


void settingswidget::acceptColorPresetPtr(colorPrefsType *cp)
{
    if(cp != NULL)
    {
        qDebug(logGui()) << "Accepting color preset pointer into settings widget.";
        colorPreset = cp;
    }
}


void settingswidget::insertClusterOutputText(QString text)
{
    ui->clusterOutputTextEdit->moveCursor(QTextCursor::End);
    ui->clusterOutputTextEdit->insertPlainText(text);
    ui->clusterOutputTextEdit->moveCursor(QTextCursor::End);
}

void settingswidget::setUItoClustersList()
{
    int defaultCluster = 0;
    int numClusters = prefs->clusters.size();
    if(numClusters > 0)
    {
        ui->clusterServerNameCombo->blockSignals(true);
        for (int f = 0; f < prefs->clusters.size(); f++)
        {
            ui->clusterServerNameCombo->addItem(prefs->clusters[f].server);
            if (prefs->clusters[f].isdefault) {
                defaultCluster = f;
            }
        }
        ui->clusterServerNameCombo->blockSignals(false);
        if (prefs->clusters.size() > defaultCluster)
        {
            ui->clusterServerNameCombo->setCurrentIndex(defaultCluster);
            ui->clusterTcpPortLineEdit->blockSignals(true);
            ui->clusterUsernameLineEdit->blockSignals(true);
            ui->clusterPasswordLineEdit->blockSignals(true);
            ui->clusterTimeoutLineEdit->blockSignals(true);
            ui->clusterTcpPortLineEdit->setText(QString::number(prefs->clusters[defaultCluster].port));
            ui->clusterUsernameLineEdit->setText(prefs->clusters[defaultCluster].userName);
            ui->clusterPasswordLineEdit->setText(prefs->clusters[defaultCluster].password);
            ui->clusterTimeoutLineEdit->setText(QString::number(prefs->clusters[defaultCluster].timeout));
            ui->clusterTcpPortLineEdit->blockSignals(false);
            ui->clusterUsernameLineEdit->blockSignals(false);
            ui->clusterPasswordLineEdit->blockSignals(false);
            ui->clusterTimeoutLineEdit->blockSignals(false);
        }
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
            qDebug(logGui()) << "Updating Rs pref" << (int)i;
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
    prefServerItem si;
    if(items & (int)s_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)s_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating ServerConfig" << (int)i;
            si = (prefServerItem)i;
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
        // Initial setup, populate combobox
        ui->clusterServerNameCombo->clear();
        for (const auto &c: prefs->clusters)
        {
            ui->clusterServerNameCombo->addItem(c.server);
        }
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
    case if_meter1Type:
    {
        // No local widget for this
        break;
    }
    case if_meter2Type:
    {
        ui->meter2selectionCombo->blockSignals(true);
        int m = ui->meter2selectionCombo->findData(prefs->meter2Type);
        ui->meter2selectionCombo->setCurrentIndex(m);
        ui->meter2selectionCombo->blockSignals(false);
        if(prefs->meter2Type != meterNone) {
            muteSingleComboItem(ui->meter3selectionCombo, m);
        }
        break;
    }
    case if_meter3Type:
    {
        ui->meter3selectionCombo->blockSignals(true);
        int m = ui->meter3selectionCombo->findData(prefs->meter3Type);
        ui->meter3selectionCombo->setCurrentIndex(m);
        ui->meter3selectionCombo->blockSignals(false);
        if(prefs->meter3Type != meterNone) {
            muteSingleComboItem(ui->meter2selectionCombo, m);
        }
        break;
    }
    case if_compMeterReverse:
    {
        quietlyUpdateCheckbox(ui->revCompMeterBtn, prefs->compMeterReverse);
        break;
    }
    case if_clickDragTuningEnable:
        quietlyUpdateCheckbox(ui->clickDragTuningEnableChk, prefs->clickDragTuningEnable);
        break;
    case if_currentColorPresetNumber:
        //ui->colorPresetCombo->blockSignals(true);
        {
            colorPrefsType p;
            for(int pn=0; pn < numColorPresetsTotal; pn++)
            {
                p = colorPreset[pn];
                if(p.presetName != Q_NULLPTR)
                    ui->colorPresetCombo->setItemText(pn, *p.presetName);
            }
            ui->colorPresetCombo->setCurrentIndex(prefs->currentColorPresetNumber);
            //ui->colorPresetCombo->blockSignals(false);
            // activate? or done when prefs load? Maybe some of each?
            // TODO
            break;
        }
    case if_rigCreatorEnable:
        quietlyUpdateCheckbox(ui->rigCreatorChk, prefs->rigCreatorEnable);
        break;
    case if_frequencyUnits:
        quietlyUpdateCombobox(ui->frequencyUnitsCombo, prefs->frequencyUnits);
        break;
    case if_region:
        quietlyUpdateLineEdit(ui->regionTxt,prefs->region);
        break;
    case if_showBands:
        quietlyUpdateCheckbox(ui->showBandsChk, prefs->showBands);
        break;
    case if_separators:
        quietlyUpdateCombobox(ui->groupSeparatorsCombo,QVariant(prefs->groupSeparator));
        quietlyUpdateCombobox(ui->decimalSeparatorsCombo,QVariant(prefs->decimalSeparator));
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
    int pos = ui->colorPresetCombo->currentIndex();
    switch(col)
    {
    case col_grid:
    {
        QColor c = (colorPreset[pos].gridColor);
        setColorElement(c,ui->colorSwatchGrid, ui->colorEditGrid);
        break;
    }
    case col_axis:
    {
        QColor c = (colorPreset[pos].axisColor);
        setColorElement(c, ui->colorSwatchAxis, ui->colorEditAxis);
        break;
    }
    case col_text:
    {
        QColor c = (colorPreset[pos].textColor);
        setColorElement(c, ui->colorSwatchText, ui->colorEditText);
        break;
    }
    case col_plotBackground:
    {
        QColor c = (colorPreset[pos].plotBackground);
        setColorElement(c, ui->colorSwatchPlotBackground, ui->colorEditPlotBackground);
        break;
    }
    case col_spectrumLine:
    {
        QColor c = (colorPreset[pos].spectrumLine);
        setColorElement(c, ui->colorSwatchSpecLine, ui->colorEditSpecLine);
        break;
    }
    case col_spectrumFill:
    {
        QColor c = (colorPreset[pos].spectrumFill);
        setColorElement(c, ui->colorSwatchSpecFill, ui->colorEditSpecFill);
        break;
    }
    case col_useSpectrumFillGradient:
    {
        quietlyUpdateCheckbox(ui->useSpectrumFillGradientChk, colorPreset[pos].useSpectrumFillGradient);
        break;
    }
    case col_spectrumFillTop:
    {
        QColor c = (colorPreset[pos].spectrumFillTop);
        setColorElement(c, ui->colorSwatchSpecFillTop, ui->colorEditSpecFillTop);
        break;
    }
    case col_spectrumFillBot:
    {
        QColor c = (colorPreset[pos].spectrumFillBot);
        setColorElement(c, ui->colorSwatchSpecFillBot, ui->colorEditSpecFillBot);
        break;
    }
    case col_underlayLine:
    {
        QColor c = (colorPreset[pos].underlayLine);
        setColorElement(c, ui->colorSwatchUnderlayLine, ui->colorEditUnderlayLine);
        break;
    }
    case col_underlayFill:
    {
        QColor c = (colorPreset[pos].underlayFill);
        setColorElement(c, ui->colorSwatchUnderlayFill, ui->colorEditUnderlayFill);
        break;
    }
    case col_useUnderlayFillGradient:
    {
        quietlyUpdateCheckbox(ui->useUnderlayFillGradientChk, colorPreset[pos].useUnderlayFillGradient);
        break;
    }
    case col_underlayFillTop:
    {
        QColor c = (colorPreset[pos].underlayFillTop);
        setColorElement(c, ui->colorSwatchUnderlayFillTop, ui->colorEditUnderlayFillTop);
        break;
    }
    case col_underlayFillBot:
    {
        QColor c = (colorPreset[pos].underlayFillBot);
        setColorElement(c, ui->colorSwatchUnderlayFillBot, ui->colorEditUnderlayFillBot);
        break;
    }
    case col_tuningLine:
    {
        QColor c = (colorPreset[pos].tuningLine);
        setColorElement(c, ui->colorSwatchTuningLine, ui->colorEditTuningLine);
        break;
    }
    case col_passband:
    {
        QColor c = (colorPreset[pos].passband);
        setColorElement(c, ui->colorSwatchPassband, ui->colorEditPassband);
        break;
    }
    case col_pbtIndicator:
    {
        QColor c = (colorPreset[pos].pbt);
        setColorElement(c, ui->colorSwatchPBT, ui->colorEditPBT);
        break;
    }
    case col_meterLevel:
    {
        QColor c = (colorPreset[pos].meterLevel);
        setColorElement(c, ui->colorSwatchMeterLevel, ui->colorEditMeterLevel);
        break;
    }
    case col_meterAverage:
    {
        QColor c = (colorPreset[pos].meterAverage);
        setColorElement(c, ui->colorSwatchMeterAverage, ui->colorEditMeterAvg);
        break;
    }
    case col_meterPeakLevel:
    {
        QColor c = (colorPreset[pos].meterPeakLevel);
        setColorElement(c, ui->colorSwatchMeterPeakLevel, ui->colorEditMeterPeakLevel);
        break;
    }
    case col_meterHighScale:
    {
        QColor c = (colorPreset[pos].meterPeakScale);
        setColorElement(c, ui->colorSwatchMeterPeakScale, ui->colorEditMeterPeakScale);
        break;
    }
    case col_meterScale:
    {
        QColor c = (colorPreset[pos].meterScale);
        setColorElement(c, ui->colorSwatchMeterScale, ui->colorEditMeterScale);
        break;
    }
    case col_meterText:
    {
        QColor c = (colorPreset[pos].meterLowText);
        setColorElement(c, ui->colorSwatchMeterText, ui->colorEditMeterText);
        break;
    }
    case col_waterfallBack:
    {
        QColor c = (colorPreset[pos].wfBackground);
        setColorElement(c, ui->colorSwatchWfBackground, ui->colorEditWfBackground);
        break;
    }
    case col_waterfallGrid:
    {
        QColor c = (colorPreset[pos].wfGrid);
        setColorElement(c, ui->colorSwatchWfGrid, ui->colorEditWfGrid);
        break;
    }
    case col_waterfallAxis:
    {
        QColor c = (colorPreset[pos].wfAxis);
        setColorElement(c, ui->colorSwatchWfAxis, ui->colorEditWfAxis);
        break;
    }
    case col_waterfallText:
    {
        QColor c = (colorPreset[pos].wfText);
        setColorElement(c, ui->colorSwatchWfText, ui->colorEditWfText);
        break;
    }
    case col_clusterSpots:
    {
        QColor c = (colorPreset[pos].clusterSpots);
        setColorElement(c, ui->colorSwatchClusterSpots, ui->colorEditClusterSpots);
        break;
    }
    case col_buttonOff:
    {
        QColor c = (colorPreset[pos].buttonOff);
        setColorElement(c, ui->colorSwatchButtonOff, ui->colorEditButtonOff);
        break;
    }
    case col_buttonOn:
    {
        QColor c = (colorPreset[pos].buttonOn);
        setColorElement(c, ui->colorSwatchButtonOn, ui->colorEditButtonOn);
        break;
    }
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
    case ra_serialEnabled:
        // Will be enabled/disabled by UDP connection prefs
        break;
    case ra_CIVisRadioModel:
        quietlyUpdateCheckbox(ui->useCIVasRigIDChk, prefs->CIVisRadioModel);
        break;
    case ra_pttType:
        quietlyUpdateCombobox(ui->pttTypeCombo, prefs->pttType);
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
        ui->serialDeviceListCombo->blockSignals(true);
        ui->serialDeviceListCombo->clear();
        ui->serialDeviceListCombo->addItem("Auto", 0);
        int i = 0;
        for(const auto &serialPortInfo: QSerialPortInfo::availablePorts())
        {
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
            ui->serialDeviceListCombo->addItem(QString("/dev/") + serialPortInfo.portName(), i++);
#else
            ui->serialDeviceListCombo->addItem(serialPortInfo.portName(), i++);
#endif
        }

#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
        ui->serialDeviceListCombo->addItem("Manual...", 256);
#endif

        ui->serialDeviceListCombo->setCurrentIndex(ui->serialDeviceListCombo->findText(prefs->serialPortRadio));
        // If the serial port doesn't exist, leave the combo blank.
        //if (ui->serialDeviceListCombo->currentIndex() == -1)
        //{
        //    ui->serialDeviceListCombo->setCurrentIndex(0);
        //}
        ui->serialDeviceListCombo->blockSignals(false);
        break;
    }
    case ra_serialPortBaud:
        quietlyUpdateCombobox(ui->baudRateCombo,QVariant::fromValue(prefs->serialPortBaud));
        break;
    case ra_virtualSerialPort:
    {

        ui->vspCombo->blockSignals(true);
        ui->vspCombo->clear();

        int i = 0;

#ifdef Q_OS_WIN
        ui->vspCombo->addItem(QString("None"), i++);

        foreach(const QSerialPortInfo & serialPortInfo, QSerialPortInfo::availablePorts())
        {
            ui->vspCombo->addItem(serialPortInfo.portName());
        }
#else
        // Provide reasonable names for the symbolic link to the pty device
#ifdef Q_OS_MAC
        QString vspName = QStandardPaths::standardLocations(QStandardPaths::DownloadLocation)[0] + "/rig-pty";
#else
        QString vspName = QDir::homePath() + "/rig-pty";
#endif
        for (i = 1; i < 8; i++) {
            ui->vspCombo->addItem(vspName + QString::number(i));

            if (QFile::exists(vspName + QString::number(i))) {
                auto* model = qobject_cast<QStandardItemModel*>(ui->vspCombo->model());
                auto* item = model->item(ui->vspCombo->count() - 1);
                item->setEnabled(false);
            }
        }
        ui->vspCombo->addItem(vspName + QString::number(i));
        ui->vspCombo->addItem(QString("None"), i++);
        ui->vspCombo->setEditable(true);
#endif
        ui->vspCombo->setCurrentIndex(ui->vspCombo->findText(prefs->virtualSerialPort));
        if (ui->vspCombo->currentIndex() == -1)
        {
            ui->vspCombo->setCurrentIndex(0);
        }
        ui->vspCombo->blockSignals(false);
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
        quietlyUpdateModCombo(ui->modInputCombo,QVariant::fromValue(prefs->inputSource[0]));
        break;
    case rs_data1Mod:
        quietlyUpdateModCombo(ui->modInputData1Combo,QVariant::fromValue(prefs->inputSource[1]));
        break;
    case rs_data2Mod:
        quietlyUpdateModCombo(ui->modInputData2Combo,QVariant::fromValue(prefs->inputSource[2]));
        break;
    case rs_data3Mod:
        quietlyUpdateModCombo(ui->modInputData3Combo,QVariant::fromValue(prefs->inputSource[3]));
        break;
    case rs_clockUseUtc:
        quietlyUpdateCheckbox(ui->useUTCChk,prefs->useUTC);
        break;
        // Not used
    case rs_setClock:
    case rs_pttOn:
    case rs_pttOff:
    case rs_satOps:
    case rs_adjRef:
    case rs_debug:
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
    case ct_USBControllersReset:
    case ct_USBControllersSetup:
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
        quietlyUpdateRadiobutton(ui->serialEnableBtn, !prefs->enableLAN);
        if(!connectedStatus) {
            ui->groupNetwork->setEnabled(prefs->enableLAN);
            ui->groupSerial->setEnabled(!prefs->enableLAN);
        }
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
    case l_tciPort:
        ui->tciServerPortTxt->setText(QString::number(prefs->tciPort));
        break;
    case l_waterfallFormat:
        quietlyUpdateCombobox(ui->waterfallFormatCombo,int(prefs->waterfallFormat));
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
        quietlyUpdateCheckbox(ui->clusterUdpGroup, prefs->clusterUdpEnable);
        break;
    case cl_clusterTcpEnable:
        quietlyUpdateCheckbox(ui->clusterTcpGroup, prefs->clusterTcpEnable);
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
    case cl_clusterTcpConnect:
    case cl_clusterTcpDisconnect:
        break;
    default:
        qWarning(logGui()) << "Did not find matching UI element for cluster preference " << (int)pcl;
        break;
    }
    updatingUIFromPrefs = false;
}

void settingswidget::updateServerConfig(prefServerItem si)
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
    case s_disableui:
        quietlyUpdateCheckbox(ui->serverDisableUIChk, serverConfig->disableUI);
        break;
    case s_lan:
        // Not used here
        break;
    case s_controlPort:
        quietlyUpdateLineEdit(ui->serverControlPortText, QString::number(serverConfig->controlPort));
        break;
    case s_civPort:
        quietlyUpdateLineEdit(ui->serverCivPortText, QString::number(serverConfig->civPort));
        break;
    case s_audioPort:
        quietlyUpdateLineEdit(ui->serverAudioPortText, QString::number(serverConfig->audioPort));
        break;
    case s_audioOutput:
        if (serverConfig->enabled) {
            ui->serverTXAudioOutputCombo->setCurrentIndex(audioDev->findOutput("Server",
                        serverConfig->rigs.first()->txAudioSetup.name,serverConfig->rigs.first()->txAudioSetup.name.isEmpty()?false:true));
            if (ui->serverTXAudioOutputCombo->currentIndex() == -1)
            {
                emit havePortError(errorType(true, serverConfig->rigs.first()->rxAudioSetup.name,
                        tr("\nServer audio output device does not exist, please check.\nTransmit audio will NOT work until this is corrected\n**** please disable the server if not required ****")));
            }
        }
        break;
    case s_audioInput:
        if (serverConfig->enabled) {
            ui->serverRXAudioInputCombo->setCurrentIndex(audioDev->findInput("Server",
                        serverConfig->rigs.first()->rxAudioSetup.name,serverConfig->rigs.first()->rxAudioSetup.name.isEmpty()?false:true));
            if (ui->serverTXAudioOutputCombo->currentIndex() == -1)
            {
                emit havePortError(errorType(true, serverConfig->rigs.first()->rxAudioSetup.name,
                        tr("\nServer audio input device does not exist, please check.\nReceive audio will NOT work until this is corrected\n**** please disable the server if not required ****")));
            }
        }
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
    prefUDPItem upi;
    if(items & (int)u_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)u_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logGui()) << "Updating UDP preference " << i;
            upi = (prefUDPItem)i;
            updateUdpPref(upi);
        }
    }
}

void settingswidget::updateUdpPref(prefUDPItem upi)
{
    updatingUIFromPrefs = true;
    switch(upi)
    {
    case u_enabled:
        // Call function directly
        on_lanEnableBtn_clicked(prefs->enableLAN);
        break;
    case u_ipAddress:
        quietlyUpdateLineEdit(ui->ipAddressTxt,udpPrefs->ipAddress);
        break;
    case u_controlLANPort:
        quietlyUpdateLineEdit(ui->controlPortTxt,QString::number(udpPrefs->controlLANPort));
        break;
    case u_serialLANPort:
        // Not used in the UI.
        break;
    case u_audioLANPort:
        // Not used in the UI.
        break;
    case u_username:
        quietlyUpdateLineEdit(ui->usernameTxt,udpPrefs->username);
        break;
    case u_password:
        quietlyUpdateLineEdit(ui->passwordTxt,udpPrefs->password);
        break;
    case u_clientName:
        // Not used in the UI.
        break;
    case u_halfDuplex:
        quietlyUpdateCombobox(ui->audioDuplexCombo,int(udpPrefs->halfDuplex));
        break;
    case u_sampleRate:
        quietlyUpdateCombobox(ui->audioSampleRateCombo,QString::number(prefs->rxSetup.sampleRate));
        break;
    case u_rxCodec:
        quietlyUpdateCombobox(ui->audioRXCodecCombo,QVariant::fromValue(prefs->rxSetup.codec));
        break;
    case u_txCodec:
        quietlyUpdateCombobox(ui->audioTXCodecCombo,QVariant::fromValue(prefs->txSetup.codec));
        break;
    case u_rxLatency:
        ui->rxLatencySlider->setValue(prefs->rxSetup.latency); // We want the signal to fire on audio devices
        break;
    case u_txLatency:
        ui->txLatencySlider->setValue(prefs->txSetup.latency);
        break;
    case u_audioInput:
        ui->audioInputCombo->setCurrentIndex(audioDev->findInput("Client", prefs->txSetup.name));
        break;
    case u_audioOutput:
        ui->audioOutputCombo->setCurrentIndex(audioDev->findOutput("Client", prefs->rxSetup.name));
        break;
    default:
        qWarning(logGui()) << "Did not find matching UI element for UDP pref item " << (int)upi;
        break;
    }
    updatingUIFromPrefs = false;
}


void settingswidget::setAudioDevicesUI()
{
    qInfo() << "Looking for inputs";
    ui->audioInputCombo->blockSignals(true);
    ui->audioInputCombo->clear();
    ui->audioInputCombo->addItems(audioDev->getInputs());
    ui->audioInputCombo->setCurrentIndex(-1);
    ui->audioInputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(audioDev->getNumCharsIn() + 30));
    ui->audioInputCombo->blockSignals(false);

    ui->serverRXAudioInputCombo->blockSignals(true);
    ui->serverRXAudioInputCombo->clear();
    ui->serverRXAudioInputCombo->addItems(audioDev->getInputs());
    ui->serverRXAudioInputCombo->setCurrentIndex(-1);
    ui->serverRXAudioInputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %0px;}").arg(audioDev->getNumCharsIn() + 30));
    ui->serverRXAudioInputCombo->blockSignals(false);

    qInfo() << "Looking for outputs";
    ui->audioOutputCombo->blockSignals(true);
    ui->audioOutputCombo->clear();
    ui->audioOutputCombo->addItems(audioDev->getOutputs());
    ui->audioOutputCombo->setCurrentIndex(-1);
    ui->audioOutputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(audioDev->getNumCharsOut() + 30));
    ui->audioOutputCombo->blockSignals(false);

    ui->serverTXAudioOutputCombo->blockSignals(true);
    ui->serverTXAudioOutputCombo->clear();
    ui->serverTXAudioOutputCombo->addItems(audioDev->getOutputs());
    ui->serverTXAudioOutputCombo->setCurrentIndex(-1);
    ui->serverTXAudioOutputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %0px;}").arg(audioDev->getNumCharsOut() + 30));
    ui->serverTXAudioOutputCombo->blockSignals(false);


    prefs->rxSetup.type = prefs->audioSystem;
    prefs->txSetup.type = prefs->audioSystem;

    if (serverConfig->enabled && !serverConfig->rigs.isEmpty())
    {
        serverConfig->rigs.first()->rxAudioSetup.type = prefs->audioSystem;
        serverConfig->rigs.first()->txAudioSetup.type = prefs->audioSystem;

        //ui->serverRXAudioInputCombo->setCurrentIndex(audioDev->findInput("Server", serverConfig->rigs.first()->rxAudioSetup.name,true));
        //ui->serverTXAudioOutputCombo->setCurrentIndex(audioDev->findOutput("Server", serverConfig->rigs.first()->txAudioSetup.name,true));
    }

    qDebug(logSystem()) << "Audio devices done.";
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

void settingswidget::hideModSource(uchar num)
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
        ui->modInputData2ComboText->setVisible(false);
        break;
    case 3:
        combo = ui->modInputData3Combo;
        ui->modInputData3ComboText->setVisible(false);
        break;
    default:
        return;
    }
    combo->setVisible(false);
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

    for (const auto &input: data)
    {
        combo->addItem(input.name, QVariant::fromValue(input));
    }

    if (data.length()==0){
        combo->addItem("None", QVariant::fromValue(rigInput()));
    }

    combo->setVisible(true);
    combo->blockSignals(false);
}

void settingswidget::populateServerUsers()
{
    // Copy data from serverConfig.users into the server UI table
    // We will assume the data are safe to use.
    bool blank = false;
    qDebug(logGui()) << "Adding server users. Size: " << serverConfig->users.size();

    QList<SERVERUSER>::iterator user = serverConfig->users.begin();
    while (user != serverConfig->users.end())
    {
        ui->serverUsersTable->insertRow(ui->serverUsersTable->rowCount());
        serverAddUserLine(ui->serverUsersTable->rowCount()-1, user->username, user->password, user->userType);
        if((user->username == "") && !blank)
            blank = true;
        user++;
    }
}

void settingswidget::serverAddUserLine(int row, const QString &user, const QString &pass, const int &type)
{
    Q_UNUSED(row)
    // migration TODO: Review these signals/slots
    ui->serverUsersTable->blockSignals(true);

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
    comboBox->insertItems(0, { tr("Admin User"), tr("Normal User"), tr("Normal with no TX","Monitor only")});
    comboBox->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    comboBox->setProperty("col", (int)2);
    comboBox->setCurrentIndex(type);
    connect(comboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 2, comboBox);

    QPushButton* button = new QPushButton();
    button->setText(tr("Delete"));
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

void settingswidget::quietlyUpdateModCombo(QComboBox *cb, QVariant val)
{
    cb->blockSignals(true);
    for (int i=0;i<cb->count();i++)
    {
        if (cb->itemData(i).value<rigInput>().type == val.value<rigInput>().type)
        {
            cb->setCurrentIndex(i);
            break;
        }
    }
    cb->blockSignals(false);
}

void settingswidget::quietlyUpdateCombobox(QComboBox *cb, QString val)
{
    cb->blockSignals(true);
    cb->setCurrentIndex(cb->findText(val));
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

void settingswidget::quietlyUpdateCheckbox(QGroupBox *gb, bool isChecked)
{
    gb->blockSignals(true);
    gb->setChecked(isChecked);
    gb->blockSignals(false);
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

void settingswidget::on_lanEnableBtn_clicked(bool checked)
{
    // TODO: prefs.enableLAN = checked;
    // TOTO? ui->connectBtn->setEnabled(true); // move to other side
    ui->groupNetwork->setEnabled(checked);
    ui->groupSerial->setEnabled(!checked);
    prefs->enableLAN = checked;
    emit changedLanPref(l_enableLAN);
}

void settingswidget::on_serialEnableBtn_clicked(bool checked)
{
    ui->groupSerial->setEnabled(checked);
    ui->groupNetwork->setEnabled(!checked);

    prefs->enableLAN = !checked;
    emit changedLanPref(l_enableLAN);
}


void settingswidget::on_pttTypeCombo_currentIndexChanged(int index)
{
    prefs->pttType = pttType_t(index);
    emit changedRaPref(ra_pttType);
}


void settingswidget::on_rigCIVManualAddrChk_clicked(bool checked)
{
    if(checked)
    {
        ui->rigCIVaddrHexLine->setEnabled(true);
        ui->rigCIVaddrHexLine->setText(QString("%1").arg(prefs->radioCIVAddr, 2, 16));
    } else {
        ui->rigCIVaddrHexLine->setText("auto");
        ui->rigCIVaddrHexLine->setEnabled(false);
        prefs->radioCIVAddr = 0; // auto
    }
    emit changedRaPref(ra_radioCIVAddr);
}

void settingswidget::on_rigCIVaddrHexLine_editingFinished()
{
    bool okconvert=false;

    quint8 propCIVAddr = (quint8) ui->rigCIVaddrHexLine->text().toUInt(&okconvert, 16);

    if(okconvert && (propCIVAddr < 0xe0) && (propCIVAddr != 0))
    {
        prefs->radioCIVAddr = propCIVAddr;
        emit changedRaPref(ra_radioCIVAddr);
    } else {
        ui->rigCIVaddrHexLine->setText("0");
    }
}

void settingswidget::on_useCIVasRigIDChk_clicked(bool checked)
{
    prefs->CIVisRadioModel = checked;
    emit changedRaPref(ra_CIVisRadioModel);
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
    ui->usbControllersSetup->setEnabled(checked);
    ui->usbControllersReset->setEnabled(checked);
    ui->usbResetLbl->setVisible(checked);
    emit changedCtPref(ct_enableUSBControllers);
}

void settingswidget::on_usbControllersSetup_clicked()
{
    emit changedCtPref(ct_USBControllersSetup);
}

void settingswidget::on_usbControllersReset_clicked()
{
    emit changedCtPref(ct_USBControllersReset);
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

void settingswidget::on_serialDeviceListCombo_textActivated(const QString &arg1)
{
    qInfo(logGui()) << "Serial port combo changed" << arg1;
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
    quietlyUpdateCombobox(ui->audioSystemServerCombo,value);
    prefs->audioSystem = static_cast<audioType>(value);
    audioDev->setAudioType(prefs->audioSystem);
    audioDev->enumerate();
    emit changedRaPref(ra_audioSystem);
}

void settingswidget::on_audioSystemServerCombo_currentIndexChanged(int value)
{
    quietlyUpdateCombobox(ui->audioSystemCombo,value);
    prefs->audioSystem = static_cast<audioType>(value);
    audioDev->setAudioType(prefs->audioSystem);
    audioDev->enumerate();
    emit changedRaPref(ra_audioSystem);
}

void settingswidget::on_meter2selectionCombo_currentIndexChanged(int index)
{
    prefs->meter2Type = static_cast<meter_t>(ui->meter2selectionCombo->itemData(index).toInt());
    emit changedIfPref(if_meter2Type);
    enableAllComboBoxItems(ui->meter3selectionCombo);
    if(prefs->meter2Type != meterNone) {
        setComboBoxItemEnabled(ui->meter3selectionCombo, index, false);
    }
}

void settingswidget::on_meter3selectionCombo_currentIndexChanged(int index)
{
    prefs->meter3Type = static_cast<meter_t>(ui->meter3selectionCombo->itemData(index).toInt());
    emit changedIfPref(if_meter3Type);
    // Since the meter combo boxes have the same items in each list,
    // we can disable an item from the other list with confidence.
    enableAllComboBoxItems(ui->meter2selectionCombo);
    if(prefs->meter3Type != meterNone) {
        setComboBoxItemEnabled(ui->meter2selectionCombo, index, false);
    }
}

void settingswidget::on_revCompMeterBtn_clicked(bool checked)
{
    prefs->compMeterReverse = checked;
    emit changedIfPref(if_compMeterReverse);
}

void settingswidget::muteSingleComboItem(QComboBox *comboBox, int index) {
    enableAllComboBoxItems(comboBox);
    setComboBoxItemEnabled(comboBox, index, false);
}

void settingswidget::enableAllComboBoxItems(QComboBox *combobox) {
    for(int i=0; i < combobox->count(); i++) {
        setComboBoxItemEnabled(combobox, i, true);
    }
}

void settingswidget::setComboBoxItemEnabled(QComboBox * comboBox, int index, bool enabled)
{

    auto * model = qobject_cast<QStandardItemModel*>(comboBox->model());
    //assert(model);
    if(!model) return;

    auto * item = model->item(index);
    //assert(item);
    if(!item) return;
    item->setEnabled(enabled);
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

void settingswidget::on_regionTxt_textChanged(QString text)
{
    prefs->region = text;
    emit changedIfPref(if_region);
}

void settingswidget::on_showBandsChk_clicked(bool checked)
{
    prefs->showBands = checked;
    emit changedIfPref(if_showBands);
}

void settingswidget::on_rigCreatorChk_clicked(bool checked)
{
    prefs->rigCreatorEnable = checked;
    emit changedIfPref(if_rigCreatorEnable);
}

void settingswidget::on_frequencyUnitsCombo_currentIndexChanged(int index)
{
    prefs->frequencyUnits = index;
    emit changedIfPref(if_frequencyUnits);
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

void settingswidget::on_tciServerPortTxt_editingFinished()
{
    bool okconvert = false;
    unsigned int port = ui->tciServerPortTxt->text().toUInt(&okconvert);
    if (okconvert)
    {
        prefs->tciPort = port;
        emit changedLanPref(l_tciPort);
    }
}

void settingswidget::on_waterfallFormatCombo_currentIndexChanged(int index)
{
    prefs->waterfallFormat = index;
    emit changedLanPref(l_waterfallFormat);
}

/* Beginning of cluster settings */


void settingswidget::on_clusterTcpAddBtn_clicked()
{
    QString text = ui->clusterServerNameCombo->lineEdit()->displayText();
    int id = ui->clusterServerNameCombo->findText(text);

    if (id == -1)
    {
        ui->clusterServerNameCombo->addItem(text);
        clusterSettings c;
        c.server = text;
        c.userName = prefs->clusterTcpUserName;
        c.password = prefs->clusterTcpPassword;
        c.port = prefs->clusterTcpPort;
        c.timeout = prefs->clusterTimeout;
        c.isdefault = true;
        prefs->clusters.append(c);
    } else {
        prefs->clusters[id].userName = prefs->clusterTcpUserName;
        prefs->clusters[id].password = prefs->clusterTcpPassword;
        prefs->clusters[id].port = prefs->clusterTcpPort;
        prefs->clusters[id].timeout = prefs->clusterTimeout;
    }
}

void settingswidget::on_clusterTcpDelBtn_clicked()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < 0)
        return;
    QString text = ui->clusterServerNameCombo->currentText();
    qInfo(logGui) << "Deleting Cluster server" << text;
    prefs->clusters.removeAt(index);
    ui->clusterServerNameCombo->removeItem(index);
    ui->clusterServerNameCombo->setCurrentIndex(0);
}

void settingswidget::on_clusterServerNameCombo_currentIndexChanged(int index)
{
    if (index < 0 || index >= prefs->clusters.size())
        return;

    quietlyUpdateLineEdit(ui->clusterTcpPortLineEdit,QString::number(prefs->clusters[index].port));
    quietlyUpdateLineEdit(ui->clusterUsernameLineEdit,prefs->clusters[index].userName);
    quietlyUpdateLineEdit(ui->clusterPasswordLineEdit,prefs->clusters[index].password);
    quietlyUpdateLineEdit(ui->clusterTimeoutLineEdit,QString::number(prefs->clusters[index].timeout));

    for (int i = 0; i < prefs->clusters.size(); i++) {
        if (i == index)
            prefs->clusters[i].isdefault = true;
        else
            prefs->clusters[i].isdefault = false;
    }

    // For reference, here is what the code on the other side is basically doing:
//    emit setClusterServerName(clusters[index].server);
//    emit setClusterTcpPort(clusters[index].port);
//    emit setClusterUserName(clusters[index].userName);
//    emit setClusterPassword(clusters[index].password);
//    emit setClusterTimeout(clusters[index].timeout);

    // We are using the not-arrayed data in preferences
    // to communicate the current cluster information:
    prefs->clusterTcpServerName = prefs->clusters[index].server;
    prefs->clusterTcpPort = prefs->clusters[index].port;
    prefs->clusterTcpUserName = prefs->clusters[index].userName;
    prefs->clusterTcpPassword = prefs->clusters[index].password;
    prefs->clusterTimeout = prefs->clusters[index].timeout;

    /*emit changedClusterPrefs(cl_clusterTcpServerName |
                             cl_clusterTcpPort |
                             cl_clusterTcpUserName |
                             cl_clusterTcpPassword |
                             cl_clusterTimeout |
                             cl_clusterTcpEnable); */
}

void settingswidget::on_clusterUdpGroup_clicked(bool checked)
{
    prefs->clusterUdpEnable = checked;
}

void settingswidget::on_clusterTcpGroup_clicked(bool checked)
{
    prefs->clusterTcpEnable = checked;
}


void settingswidget::on_clusterServerNameCombo_currentTextChanged(const QString &text)
{
    Q_UNUSED(text)
    ui->clusterTcpAddBtn->setEnabled(true);
}

void settingswidget::on_clusterTcpPortLineEdit_editingFinished()
{
    prefs->clusterTcpPort = ui->clusterTcpPortLineEdit->displayText().toInt();
}

void settingswidget::on_clusterUsernameLineEdit_editingFinished()
{
    prefs->clusterTcpUserName = ui->clusterUsernameLineEdit->text();
}

void settingswidget::on_clusterPasswordLineEdit_editingFinished()
{
    prefs->clusterTcpPassword =  ui->clusterPasswordLineEdit->text();
}

void settingswidget::on_clusterTimeoutLineEdit_editingFinished()
{
    prefs->clusterTimeout = ui->clusterTimeoutLineEdit->displayText().toInt();
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

void settingswidget::on_clusterTcpConnectBtn_clicked()
{
    emit changedClusterPref(cl_clusterTcpConnect);
}

void settingswidget::on_clusterTcpDisconnectBtn_clicked()
{
    emit changedClusterPref(cl_clusterTcpDisconnect);
}


/* End of cluster settings */


/* Beginning of UDP connection settings */
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

void settingswidget::on_audioSampleRateCombo_currentIndexChanged(int value)
{
    prefs->rxSetup.sampleRate= ui->audioSampleRateCombo->itemText(value).toInt();
    prefs->txSetup.sampleRate= ui->audioSampleRateCombo->itemText(value).toInt();
    emit changedUdpPref(u_sampleRate);
}

void settingswidget::on_audioRXCodecCombo_currentIndexChanged(int value)
{
    prefs->rxSetup.codec = ui->audioRXCodecCombo->itemData(value).toInt();
    emit changedUdpPref(u_rxCodec);
}

void settingswidget::on_audioTXCodecCombo_currentIndexChanged(int value)
{
    prefs->txSetup.codec = ui->audioTXCodecCombo->itemData(value).toInt();
    emit changedUdpPref(u_txCodec);
}

void settingswidget::on_rxLatencySlider_valueChanged(int value)
{
    prefs->rxSetup.latency = value;
    ui->rxLatencyValue->setText(QString::number(value));
    emit changedUdpPref(u_rxLatency);
}

void settingswidget::on_txLatencySlider_valueChanged(int value)
{
    //txSetup.latency = value;
    prefs->txSetup.latency = value;
    ui->txLatencyValue->setText(QString::number(value));
    emit changedUdpPref(u_txLatency);
}

void settingswidget::on_audioOutputCombo_currentIndexChanged(int index)
{
    if (index >= 0) {
        if (prefs->audioSystem == qtAudio) {
            prefs->rxSetup.port = audioDev->getOutputDeviceInfo(index);
        }
        else {
            prefs->rxSetup.portInt = audioDev->getOutputDeviceInt(index);
        }

        prefs->rxSetup.name = audioDev->getOutputName(index);
    }
    qDebug(logGui()) << "Changed audio output to:" << prefs->rxSetup.name;

    emit changedUdpPref(u_audioOutput);
}

void settingswidget::on_audioInputCombo_currentIndexChanged(int index)
{
    if (index >= 0) {
        if (prefs->audioSystem == qtAudio) {
            prefs->txSetup.port = audioDev->getInputDeviceInfo(index);
        }
        else {
            prefs->txSetup.portInt = audioDev->getInputDeviceInt(index);
        }

        prefs->txSetup.name = audioDev->getInputName(index);
    }
    qDebug(logGui()) << "Changed audio output to:" << prefs->rxSetup.name;

    emit changedUdpPref(u_audioInput);
}

/* End of UDP connection settings */



/* Beginning of radio specific settings */
void settingswidget::on_modInputCombo_activated(int index)
{
    prefs->inputSource[0]= ui->modInputCombo->itemData(index).value<rigInput>();
    emit changedRsPref(rs_dataOffMod);
}

void settingswidget::on_modInputData1Combo_activated(int index)
{
    prefs->inputSource[1] = ui->modInputData1Combo->itemData(index).value<rigInput>();
    emit changedRsPref(rs_data1Mod);
}


void settingswidget::on_modInputData2Combo_activated(int index)
{
    prefs->inputSource[2] = ui->modInputData2Combo->itemData(index).value<rigInput>();
    emit changedRsPref(rs_data2Mod);
}


void settingswidget::on_modInputData3Combo_activated(int index)
{
    prefs->inputSource[3] = ui->modInputData3Combo->itemData(index).value<rigInput>();
    emit changedRsPref(rs_data3Mod);
}

void settingswidget::on_decimalSeparatorsCombo_currentIndexChanged(int index)
{
    prefs->decimalSeparator = ui->decimalSeparatorsCombo->itemData(index).toChar();
    emit changedIfPref(if_separators);
}

void settingswidget::on_groupSeparatorsCombo_currentIndexChanged(int index)
{
    prefs->groupSeparator = ui->groupSeparatorsCombo->itemData(index).toChar();
    emit changedIfPref(if_separators);
}

/* End of radio specific settings */


/* Color UI helper functions */
void settingswidget::setColorElement(QColor color,
                             QLedLabel *led,
                             QLabel *label,
                             QLineEdit *lineText)
{
    if(led != Q_NULLPTR)
    {
        led->setColor(color, true);
    }
    if(label != Q_NULLPTR)
    {
        label->setText(color.name(QColor::HexArgb));
    }
    if(lineText != Q_NULLPTR)
    {
        lineText->setText(color.name(QColor::HexArgb));
    }
}

void settingswidget::setColorElement(QColor color, QLedLabel *led, QLabel *label)
{
    setColorElement(color, led, label, Q_NULLPTR);
}

void settingswidget::setColorElement(QColor color, QLedLabel *led, QLineEdit *lineText)
{
    setColorElement(color, led, Q_NULLPTR, lineText);
}

QColor settingswidget::getColorFromPicker(QColor initialColor)
{
    QColorDialog::ColorDialogOptions options;
    options.setFlag(QColorDialog::ShowAlphaChannel, true);
    options.setFlag(QColorDialog::DontUseNativeDialog, true);
    QColor selColor = QColorDialog::getColor(initialColor, this, "Select Color", options);
    int alphaVal = 0;
    bool ok = false;

    if (selColor.isValid())
    {
        if (selColor.alpha() == 0)
        {
            alphaVal = QInputDialog::getInt(this, tr("Specify Opacity"),
                                            tr("You specified an opacity value of 0. \nDo you want to change it? (0=transparent, 255=opaque)"), 0, 0, 255, 1,
                                            &ok);
            if (!ok)
            {
                return selColor;
            }
            else {
                selColor.setAlpha(alphaVal);
                return selColor;
            }
        }
        return selColor;
    }
    else
        return initialColor;
}

void settingswidget::getSetColor(QLedLabel *led, QLabel *label)
{
    QColor selColor = getColorFromPicker(led->getColor());
    setColorElement(selColor, led, label);
}

void settingswidget::getSetColor(QLedLabel *led, QLineEdit *line)
{
    QColor selColor = getColorFromPicker(led->getColor());
    setColorElement(selColor, led, line);
}

QString settingswidget::setColorFromString(QString colorstr, QLedLabel *led)
{
    if(led==Q_NULLPTR)
        return "ERROR";

    if(!colorstr.startsWith("#"))
    {
        colorstr.prepend("#");
    }
    if(colorstr.length() != 9)
    {
        // TODO: Tell user about AA RR GG BB
        return led->getColor().name(QColor::HexArgb);
    }
    led->setColor(colorstr, true);
    return led->getColor().name(QColor::HexArgb);
}

void settingswidget::setColorButtonOperations(QColor *colorStore,
                                      QLineEdit *e, QLedLabel *d)
{
    // Call this function with a pointer into the colorPreset color you
    // wish to edit.

    if(colorStore==Q_NULLPTR)
    {
        qInfo(logSystem()) << "ERROR, invalid pointer to color received.";
        return;
    }
    getSetColor(d, e);
    QColor t = d->getColor();
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    *(colorStore) = colorStore->fromString(t.name(QColor::HexArgb));
#else
    colorStore->setNamedColor(t.name(QColor::HexArgb));
#endif
    //useCurrentColorPreset();
}

void settingswidget::setColorLineEditOperations(QColor *colorStore,
                                        QLineEdit *e, QLedLabel *d)
{
    // Call this function with a pointer into the colorPreset color you
    // wish to edit.
    if(colorStore==Q_NULLPTR)
    {
        qInfo(logSystem()) << "ERROR, invalid pointer to color received.";
        return;
    }

    QString colorStrValidated = setColorFromString(e->text(), d);
    e->setText(colorStrValidated);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    *(colorStore) = colorStore->fromString(colorStrValidated);
#else
    colorStore->setNamedColor(colorStrValidated);
#endif
    //useCurrentColorPreset();
}

void settingswidget::setEditAndLedFromColor(QColor c, QLineEdit *e, QLedLabel *d)
{
    bool blockSignals = true;
    if(e != Q_NULLPTR)
    {
        e->blockSignals(blockSignals);
        e->setText(c.name(QColor::HexArgb));
        e->blockSignals(false);
    }
    if(d != Q_NULLPTR)
    {
        d->setColor(c);
    }
}



void settingswidget::loadColorPresetToUIandPlots(int presetNumber)
{
    if(presetNumber >= numColorPresetsTotal)
    {
        qDebug(logSystem()) << "WARNING: asked for preset number [" << presetNumber << "], which is out of range.";
        return;
    }

    colorPrefsType p = colorPreset[presetNumber];
    //qInfo(logSystem()) << "color preset number [" << presetNumber << "] requested for UI load, which has internal index of [" << p.presetNum << "]";
    setEditAndLedFromColor(p.gridColor, ui->colorEditGrid, ui->colorSwatchGrid);
    setEditAndLedFromColor(p.axisColor, ui->colorEditAxis, ui->colorSwatchAxis);
    setEditAndLedFromColor(p.textColor, ui->colorEditText, ui->colorSwatchText);
    setEditAndLedFromColor(p.spectrumLine, ui->colorEditSpecLine, ui->colorSwatchSpecLine);
    setEditAndLedFromColor(p.spectrumFill, ui->colorEditSpecFill, ui->colorSwatchSpecFill);
    quietlyUpdateCheckbox(ui->useSpectrumFillGradientChk, p.useSpectrumFillGradient);
    setEditAndLedFromColor(p.spectrumFillTop, ui->colorEditSpecFillTop, ui->colorSwatchSpecFillTop);
    setEditAndLedFromColor(p.spectrumFillBot, ui->colorEditSpecFillBot, ui->colorSwatchSpecFillBot);
    setEditAndLedFromColor(p.underlayLine, ui->colorEditUnderlayLine, ui->colorSwatchUnderlayLine);
    setEditAndLedFromColor(p.underlayFill, ui->colorEditUnderlayFill, ui->colorSwatchUnderlayFill);
    setEditAndLedFromColor(p.underlayFillTop, ui->colorEditUnderlayFillTop, ui->colorSwatchUnderlayFillTop);
    setEditAndLedFromColor(p.underlayFillBot, ui->colorEditUnderlayFillBot, ui->colorSwatchUnderlayFillBot);
    quietlyUpdateCheckbox(ui->useUnderlayFillGradientChk, p.useUnderlayFillGradient);
    setEditAndLedFromColor(p.plotBackground, ui->colorEditPlotBackground, ui->colorSwatchPlotBackground);

    setEditAndLedFromColor(p.tuningLine, ui->colorEditTuningLine, ui->colorSwatchTuningLine);
    setEditAndLedFromColor(p.passband, ui->colorEditPassband, ui->colorSwatchPassband);
    setEditAndLedFromColor(p.pbt, ui->colorEditPBT, ui->colorSwatchPBT);

    setEditAndLedFromColor(p.meterLevel, ui->colorEditMeterLevel, ui->colorSwatchMeterLevel);
    setEditAndLedFromColor(p.meterAverage, ui->colorEditMeterAvg, ui->colorSwatchMeterAverage);
    setEditAndLedFromColor(p.meterPeakLevel, ui->colorEditMeterPeakLevel, ui->colorSwatchMeterPeakLevel);
    setEditAndLedFromColor(p.meterPeakScale, ui->colorEditMeterPeakScale, ui->colorSwatchMeterPeakScale);
    setEditAndLedFromColor(p.meterLowerLine, ui->colorEditMeterScale, ui->colorSwatchMeterScale);
    setEditAndLedFromColor(p.meterLowText, ui->colorEditMeterText, ui->colorSwatchMeterText);

    setEditAndLedFromColor(p.wfBackground, ui->colorEditWfBackground, ui->colorSwatchWfBackground);
    setEditAndLedFromColor(p.wfGrid, ui->colorEditWfGrid, ui->colorSwatchWfGrid);
    setEditAndLedFromColor(p.wfAxis, ui->colorEditWfAxis, ui->colorSwatchWfAxis);
    setEditAndLedFromColor(p.wfText, ui->colorEditWfText, ui->colorSwatchWfText);

    setEditAndLedFromColor(p.clusterSpots, ui->colorEditClusterSpots, ui->colorSwatchClusterSpots);
    setEditAndLedFromColor(p.buttonOff, ui->colorEditButtonOff, ui->colorSwatchButtonOff);
    setEditAndLedFromColor(p.buttonOn, ui->colorEditButtonOn, ui->colorSwatchButtonOn);

    //useColorPreset(&p);
    prefs->currentColorPresetNumber = presetNumber;
    emit changedIfPref(if_currentColorPresetNumber);
}

void settingswidget::on_colorRenamePresetBtn_clicked()
{
    int p = ui->colorPresetCombo->currentIndex();
    QString newName;
    QMessageBox msgBox;

    bool ok = false;
    newName = QInputDialog::getText(this, tr("Rename Preset"),
                                    tr("Preset Name (10 characters max):"), QLineEdit::Normal,
                                    ui->colorPresetCombo->currentText(), &ok);
    if(!ok)
        return;

    if(ok && (newName.length() < 11) && !newName.isEmpty())
    {
        colorPreset[p].presetName->clear();
        colorPreset[p].presetName->append(newName);
        ui->colorPresetCombo->setItemText(p, *(colorPreset[p].presetName));
    } else {
        if(newName.isEmpty() || (newName.length() > 10))
        {
            msgBox.setText("Error, name must be at least one character and not exceed 10 characters.");
            msgBox.exec();
        }
    }
}

void settingswidget::on_colorPresetCombo_currentIndexChanged(int index)
{
    prefs->currentColorPresetNumber = index;
    loadColorPresetToUIandPlots(index);
}

void settingswidget::on_colorRevertPresetBtn_clicked()
{
    int pn = ui->colorPresetCombo->currentIndex();
    //setDefaultColors(pn);
    loadColorPresetToUIandPlots(pn);
}

// ---------- end color helper functions ---------- //


// ----------       Color UI slots        ----------//

// Removed save preset button.

// Grid:
void settingswidget::on_colorSetBtnGrid_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].gridColor);
    setColorButtonOperations(c, ui->colorEditGrid, ui->colorSwatchGrid);
    emit changedColPref(col_grid);
}
void settingswidget::on_colorEditGrid_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].gridColor);
    setColorLineEditOperations(c, ui->colorEditGrid, ui->colorSwatchGrid);
    emit changedColPref(col_grid);
}

// Axis:
void settingswidget::on_colorSetBtnAxis_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].axisColor);
    setColorButtonOperations(c, ui->colorEditAxis, ui->colorSwatchAxis);
    emit changedColPref(col_axis);
}
void settingswidget::on_colorEditAxis_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].axisColor);
    setColorLineEditOperations(c, ui->colorEditAxis, ui->colorSwatchAxis);
    emit changedColPref(col_axis);
}

// Text:
void settingswidget::on_colorSetBtnText_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].textColor);
    setColorButtonOperations(c, ui->colorEditText, ui->colorSwatchText);
    emit changedColPref(col_text);
}
void settingswidget::on_colorEditText_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].textColor);
    setColorLineEditOperations(c, ui->colorEditText, ui->colorSwatchText);
    emit changedColPref(col_text);
}

// SpecLine:
void settingswidget::on_colorEditSpecLine_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumLine);
    setColorLineEditOperations(c, ui->colorEditSpecLine, ui->colorSwatchSpecLine);
    emit changedColPref(col_spectrumLine);
}
void settingswidget::on_colorSetBtnSpecLine_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumLine);
    setColorButtonOperations(c, ui->colorEditSpecLine, ui->colorSwatchSpecLine);
    emit changedColPref(col_spectrumLine);
}

// SpecFill:
void settingswidget::on_colorSetBtnSpecFill_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumFill);
    setColorButtonOperations(c, ui->colorEditSpecFill, ui->colorSwatchSpecFill);
    emit changedColPref(col_spectrumFill);
}
void settingswidget::on_colorEditSpecFill_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumFill);
    setColorLineEditOperations(c, ui->colorEditSpecFill, ui->colorSwatchSpecFill);
    emit changedColPref(col_spectrumFill);
}
void settingswidget::on_useSpectrumFillGradientChk_clicked(bool checked)
{
    int pos = ui->colorPresetCombo->currentIndex();
    colorPreset[pos].useSpectrumFillGradient = checked;
    emit changedColPref(col_useSpectrumFillGradient);
}

// SpecFill Top:
void settingswidget::on_colorSetBtnSpectFillTop_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumFillTop);
    setColorButtonOperations(c, ui->colorEditSpecFillTop, ui->colorSwatchSpecFillTop);
    emit changedColPref(col_spectrumFillTop);
}
void settingswidget::on_colorEditSpecFillTop_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumFillTop);
    setColorLineEditOperations(c, ui->colorEditSpecFillTop, ui->colorSwatchSpecFillTop);
    emit changedColPref(col_spectrumFillTop);
}

// SpecFill Bot:
void settingswidget::on_colorSetBtnSpectFillBot_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumFillBot);
    setColorButtonOperations(c, ui->colorEditSpecFillBot, ui->colorSwatchSpecFillBot);
    emit changedColPref(col_spectrumFillBot);
}
void settingswidget::on_colorEditSpecFillBot_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumFillBot);
    setColorLineEditOperations(c, ui->colorEditSpecFillBot, ui->colorSwatchSpecFillBot);
    emit changedColPref(col_spectrumFillBot);
}

// PlotBackground:
void settingswidget::on_colorEditPlotBackground_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].plotBackground);
    setColorLineEditOperations(c, ui->colorEditPlotBackground, ui->colorSwatchPlotBackground);
    emit changedColPref(col_plotBackground);
}
void settingswidget::on_colorSetBtnPlotBackground_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].plotBackground);
    setColorButtonOperations(c, ui->colorEditPlotBackground, ui->colorSwatchPlotBackground);
    emit changedColPref(col_plotBackground);
}

// Underlay Line:
void settingswidget::on_colorSetBtnUnderlayLine_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayLine);
    setColorButtonOperations(c, ui->colorEditUnderlayLine, ui->colorSwatchUnderlayLine);
    emit changedColPref(col_underlayLine);
}

void settingswidget::on_colorEditUnderlayLine_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayLine);
    setColorLineEditOperations(c, ui->colorEditUnderlayLine, ui->colorSwatchUnderlayLine);
    emit changedColPref(col_underlayLine);
}

// Underlay Fill:
void settingswidget::on_colorSetBtnUnderlayFill_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayFill);
    setColorButtonOperations(c, ui->colorEditUnderlayFill, ui->colorSwatchUnderlayFill);
    emit changedColPref(col_underlayFill);
}
void settingswidget::on_colorEditUnderlayFill_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayFill);
    setColorLineEditOperations(c, ui->colorEditUnderlayFill, ui->colorSwatchUnderlayFill);
    emit changedColPref(col_underlayFill);
}
void settingswidget::on_useUnderlayFillGradientChk_clicked(bool checked)
{
    int pos = ui->colorPresetCombo->currentIndex();
    colorPreset[pos].useUnderlayFillGradient = checked;
    emit changedColPref(col_useUnderlayFillGradient);
}

// Underlay Fill Top:
void settingswidget::on_colorSetBtnUnderlayFillTop_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayFillTop);
    setColorButtonOperations(c, ui->colorEditUnderlayFillTop, ui->colorSwatchUnderlayFillTop);
    emit changedColPref(col_underlayFillTop);
}
void settingswidget::on_colorEditUnderlayFillTop_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayFillTop);
    setColorLineEditOperations(c, ui->colorEditUnderlayFillTop, ui->colorSwatchUnderlayFillTop);
    emit changedColPref(col_underlayFillTop);
}

// Underlay Fill Bot:
void settingswidget::on_colorSetBtnUnderlayFillBot_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayFillBot);
    setColorButtonOperations(c, ui->colorEditUnderlayFillBot, ui->colorSwatchUnderlayFillBot);
    emit changedColPref(col_underlayFillBot);
}
void settingswidget::on_colorEditUnderlayFillBot_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayFillBot);
    setColorLineEditOperations(c, ui->colorEditUnderlayFillBot, ui->colorSwatchUnderlayFillBot);
    emit changedColPref(col_underlayFillBot);
}

// WF Background:
void settingswidget::on_colorSetBtnwfBackground_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfBackground);
    setColorButtonOperations(c, ui->colorEditWfBackground, ui->colorSwatchWfBackground);
    emit changedColPref(col_waterfallBack);
}
void settingswidget::on_colorEditWfBackground_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfBackground);
    setColorLineEditOperations(c, ui->colorEditWfBackground, ui->colorSwatchWfBackground);
    emit changedColPref(col_waterfallBack);
}

// WF Grid:
void settingswidget::on_colorSetBtnWfGrid_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfGrid);
    setColorButtonOperations(c, ui->colorEditWfGrid, ui->colorSwatchWfGrid);
    emit changedColPref(col_waterfallGrid);
}
void settingswidget::on_colorEditWfGrid_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfGrid);
    setColorLineEditOperations(c, ui->colorEditWfGrid, ui->colorSwatchWfGrid);
    emit changedColPref(col_waterfallGrid);
}

// WF Axis:
void settingswidget::on_colorSetBtnWfAxis_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfAxis);
    setColorButtonOperations(c, ui->colorEditWfAxis, ui->colorSwatchWfAxis);
    emit changedColPref(col_waterfallAxis);
}
void settingswidget::on_colorEditWfAxis_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfAxis);
    setColorLineEditOperations(c, ui->colorEditWfAxis, ui->colorSwatchWfAxis);
    emit changedColPref(col_waterfallAxis);
}

// WF Text:
void settingswidget::on_colorSetBtnWfText_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].wfText);
    setColorButtonOperations(c, ui->colorEditWfText, ui->colorSwatchWfText);
    emit changedColPref(col_waterfallText);
}

void settingswidget::on_colorEditWfText_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].wfText);
    setColorLineEditOperations(c, ui->colorEditWfText, ui->colorSwatchWfText);
    emit changedColPref(col_waterfallText);
}

// Tuning Line:
void settingswidget::on_colorSetBtnTuningLine_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].tuningLine);
    setColorButtonOperations(c, ui->colorEditTuningLine, ui->colorSwatchTuningLine);
    emit changedColPref(col_tuningLine);
}
void settingswidget::on_colorEditTuningLine_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].tuningLine);
    setColorLineEditOperations(c, ui->colorEditTuningLine, ui->colorSwatchTuningLine);
    emit changedColPref(col_tuningLine);
}

// Passband:
void settingswidget::on_colorSetBtnPassband_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].passband);
    setColorButtonOperations(c, ui->colorEditPassband, ui->colorSwatchPassband);
    emit changedColPref(col_passband);
}

void settingswidget::on_colorEditPassband_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].passband);
    setColorLineEditOperations(c, ui->colorEditPassband, ui->colorSwatchPassband);
    emit changedColPref(col_passband);
}

void settingswidget::on_colorSetBtnPBT_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].pbt);
    setColorButtonOperations(c, ui->colorEditPBT, ui->colorSwatchPBT);
    emit changedColPref(col_pbtIndicator);
}

void settingswidget::on_colorEditPBT_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].pbt);
    setColorLineEditOperations(c, ui->colorEditPBT, ui->colorSwatchPBT);
    emit changedColPref(col_pbtIndicator);
}

// Meter Level:
void settingswidget::on_colorSetBtnMeterLevel_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLevel);
    setColorButtonOperations(c, ui->colorEditMeterLevel, ui->colorSwatchMeterLevel);
    emit changedColPref(col_meterLevel);
}
void settingswidget::on_colorEditMeterLevel_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLevel);
    setColorLineEditOperations(c, ui->colorEditMeterLevel, ui->colorSwatchMeterLevel);
    emit changedColPref(col_meterLevel);
}

// Meter Average:
void settingswidget::on_colorSetBtnMeterAvg_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterAverage);
    setColorButtonOperations(c, ui->colorEditMeterAvg, ui->colorSwatchMeterAverage);
    emit changedColPref(col_meterAverage);
}
void settingswidget::on_colorEditMeterAvg_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterAverage);
    setColorLineEditOperations(c, ui->colorEditMeterAvg, ui->colorSwatchMeterAverage);
    emit changedColPref(col_meterAverage);
}

// Meter Peak Level:
void settingswidget::on_colorSetBtnMeterPeakLevel_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterPeakLevel);
    setColorButtonOperations(c, ui->colorEditMeterPeakLevel, ui->colorSwatchMeterPeakLevel);
    emit changedColPref(col_meterPeakLevel);
}
void settingswidget::on_colorEditMeterPeakLevel_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterPeakLevel);
    setColorLineEditOperations(c, ui->colorEditMeterPeakLevel, ui->colorSwatchMeterPeakLevel);
    emit changedColPref(col_meterPeakLevel);
}

// Meter Peak Scale:
void settingswidget::on_colorSetBtnMeterPeakScale_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterPeakScale);
    setColorButtonOperations(c, ui->colorEditMeterPeakScale, ui->colorSwatchMeterPeakScale);
    emit changedColPref(col_meterHighScale);
}
void settingswidget::on_colorEditMeterPeakScale_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterPeakScale);
    setColorLineEditOperations(c, ui->colorEditMeterPeakScale, ui->colorSwatchMeterPeakScale);
    emit changedColPref(col_meterHighScale);
}

// Meter Scale (line):
void settingswidget::on_colorSetBtnMeterScale_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLowerLine);
    setColorButtonOperations(c, ui->colorEditMeterScale, ui->colorSwatchMeterScale);
    emit changedColPref(col_meterScale);
}
void settingswidget::on_colorEditMeterScale_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLowerLine);
    setColorLineEditOperations(c, ui->colorEditMeterScale, ui->colorSwatchMeterScale);
    emit changedColPref(col_meterScale);
}

// Meter Text:
void settingswidget::on_colorSetBtnMeterText_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLowText);
    setColorButtonOperations(c, ui->colorEditMeterText, ui->colorSwatchMeterText);
    emit changedColPref(col_meterText);
}
void settingswidget::on_colorEditMeterText_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLowText);
    setColorLineEditOperations(c, ui->colorEditMeterText, ui->colorSwatchMeterText);
    emit changedColPref(col_meterText);
}

// Cluster Spots:
void settingswidget::on_colorSetBtnClusterSpots_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].clusterSpots);
    setColorButtonOperations(c, ui->colorEditClusterSpots, ui->colorSwatchClusterSpots);
    emit changedColPref(col_clusterSpots);
}
void settingswidget::on_colorEditClusterSpots_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].clusterSpots);
    setColorLineEditOperations(c, ui->colorEditClusterSpots, ui->colorSwatchClusterSpots);
    emit changedColPref(col_clusterSpots);
}


// Buttons:
void settingswidget::on_colorSetBtnButtonOff_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].buttonOff);
    setColorButtonOperations(c, ui->colorEditButtonOff, ui->colorSwatchButtonOff);
    emit changedColPref(col_buttonOff);
}
void settingswidget::on_colorEditButtonOff_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].buttonOff);
    setColorLineEditOperations(c, ui->colorEditButtonOff, ui->colorSwatchButtonOff);
    emit changedColPref(col_buttonOff);
}
void settingswidget::on_colorSetBtnButtonOn_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].buttonOn);
    setColorButtonOperations(c, ui->colorEditButtonOn, ui->colorSwatchButtonOn);
    emit changedColPref(col_buttonOn);
}
void settingswidget::on_colorEditButtonOn_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].buttonOn);
    setColorLineEditOperations(c, ui->colorEditButtonOn, ui->colorSwatchButtonOn);
    emit changedColPref(col_buttonOn);
}

// ----------   End color UI slots        ----------//

void settingswidget::on_useUTCChk_clicked(bool checked)
{
    prefs->useUTC=checked;
}

void settingswidget::on_setClockBtn_clicked()
{
    emit changedRsPref(rs_setClock);
}


void settingswidget::on_pttOnBtn_clicked()
{
    emit changedRsPref(rs_pttOn);
}

void settingswidget::on_pttOffBtn_clicked()
{
    emit changedRsPref(rs_pttOff);
}


void settingswidget::on_adjRefBtn_clicked()
{
    emit changedRsPref(rs_adjRef);
}

void settingswidget::on_satOpsBtn_clicked()
{
    emit changedRsPref(rs_satOps);
}

/* Beginning of UDP Server settings */
void settingswidget::on_serverRXAudioInputCombo_currentIndexChanged(int index)
{
    if (index >= 0) {
        if (prefs->audioSystem == qtAudio) {
            serverConfig->rigs.first()->rxAudioSetup.port = audioDev->getInputDeviceInfo(index);
        }
        else {
            serverConfig->rigs.first()->rxAudioSetup.portInt = audioDev->getInputDeviceInt(index);
        }
        serverConfig->rigs.first()->rxAudioSetup.name = audioDev->getInputName(index);
    }
    qDebug(logGui()) << "Changed server audio input to:" << serverConfig->rigs.first()->rxAudioSetup.name;

    emit changedServerPref(s_audioInput);
}

void settingswidget::on_serverTXAudioOutputCombo_currentIndexChanged(int index)
{

    if (index >= 0) {
        if (prefs->audioSystem == qtAudio) {
            serverConfig->rigs.first()->txAudioSetup.port = audioDev->getOutputDeviceInfo(index);
        }
        else {
            serverConfig->rigs.first()->txAudioSetup.portInt = audioDev->getOutputDeviceInt(index);
        }
        serverConfig->rigs.first()->txAudioSetup.name = audioDev->getOutputName(index);
    }
    qDebug(logGui()) << "Changed server audio output to:" << serverConfig->rigs.first()->txAudioSetup.name;

    emit changedServerPref(s_audioOutput);
}

void settingswidget::on_serverEnableCheckbox_clicked(bool checked)
{
    serverConfig->enabled = checked;
    emit changedServerPref(s_enabled);
}

void settingswidget::on_serverDisableUIChk_clicked(bool checked)
{
    serverConfig->disableUI = checked;
    emit changedServerPref(s_disableui);
}

void settingswidget::on_serverControlPortText_textChanged(QString text)
{
    if(!haveServerConfig)
    {
        qCritical(logGui()) << "Cannot modify users without valid serverConfig.";
        return;
    }
    serverConfig->controlPort = text.toInt();
    emit changedServerPref(s_controlPort);
}

void settingswidget::on_serverCivPortText_textChanged(QString text)
{
    if(!haveServerConfig)
    {
        qCritical(logGui()) << "Cannot modify users without valid serverConfig.";
        return;
    }
    serverConfig->civPort = text.toInt();
    emit changedServerPref(s_civPort);
}

void settingswidget::on_serverAudioPortText_textChanged(QString text)
{
    if(!haveServerConfig)
    {
        qCritical(logGui()) << "Cannot modify users without valid serverConfig.";
        return;
    }
    serverConfig->audioPort = text.toInt();
    emit changedServerPref(s_audioPort);
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
    }
}
/* End of UDP Server settings */

// This is a slot that receives a signal from wfmain when we are connecting/disconnected
void settingswidget::connectionStatus(bool conn)
{
    connectedStatus = conn;
    ui->groupConnection->setEnabled(!conn);
    ui->audioInputCombo->setEnabled(!conn);
    ui->audioOutputCombo->setEnabled(!conn);
    ui->audioDuplexCombo->setEnabled(!conn);
    ui->audioRXCodecCombo->setEnabled(!conn);
    ui->audioTXCodecCombo->setEnabled(!conn);
    ui->audioSystemCombo->setEnabled(!conn);
    ui->audioSampleRateCombo->setEnabled(!conn);

    ui->txLatencySlider->setEnabled(!conn);
    ui->usernameTxt->setEnabled(!conn);
    ui->passwordTxt->setEnabled(!conn);
    ui->controlPortTxt->setEnabled(!conn);
    ui->ipAddressTxt->setEnabled(!conn);

    ui->serialDeviceListCombo->setEnabled(!conn);
    ui->baudRateCombo->setEnabled(!conn);
    ui->pttTypeLabel->setEnabled(!conn);
    ui->pttTypeCombo->setEnabled(!conn);

    ui->serverEnableCheckbox->setEnabled(!conn);
    ui->serverDisableUIChk->setEnabled(!conn);
    ui->serverRXAudioInputCombo->setEnabled(!conn);
    ui->serverTXAudioOutputCombo->setEnabled(!conn);
    ui->audioSystemServerCombo->setEnabled(!conn);
    ui->serverControlPortText->setEnabled(!conn);
    ui->serverAudioPortText->setEnabled(!conn);
    ui->serverCivPortText->setEnabled(!conn);
    ui->serverUsersTable->setEnabled(!conn);
    ui->serverDisconnectLabel->setVisible(conn);
    ui->radioDisconnectLabel->setVisible(conn);

    if(conn) {
        ui->connectBtn->setText("Disconnect from radio");
    } else {
        ui->connectBtn->setText("Connect to radio");
    }
}

void settingswidget::on_connectBtn_clicked()
{
    emit connectButtonPressed();
}

void settingswidget::on_saveSettingsBtn_clicked()
{
    emit saveSettingsButtonPressed();
}

void settingswidget::on_revertSettingsBtn_clicked()
{
    emit revertSettingsButtonPressed();
}
