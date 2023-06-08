#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QStandardItemModel>
#include <QWidget>
#include <QList>
#include <QInputDialog>
#include <QFile>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QStringList>

#include "logcategories.h"
#include "prefs.h"
#include "colorprefs.h"
#include "udpbase.h" // for udp preferences
#include "cluster.h" // for clusterSettings
#include "rigidentities.h" // for rigInputs
#include "udpserver.h" // for SERVERCONFIG
#include "qledlabel.h" // For color swatches
#include "audiodevices.h"

namespace Ui {
class settingswidget;
}

class settingswidget : public QWidget
{
    Q_OBJECT

public:
    explicit settingswidget(QWidget *parent = nullptr);

    ~settingswidget();

public slots:
    void acceptPreferencesPtr(preferences *pptr);
    void acceptUdpPreferencesPtr(udpPreferences *upptr);
    void acceptServerConfig(SERVERCONFIG *serverConfig);
    void acceptColorPresetPtr(colorPrefsType *cp);

    void copyClusterList(QList<clusterSettings> c);
    void insertClusterOutputText(QString text);

    void updateIfPrefs(quint64 items);
    void updateColPrefs(quint64 items);
    void updateRaPrefs(quint64 items);
    void updateRsPrefs(quint64 items);
    void updateCtPrefs(quint64 items);
    void updateLanPrefs(quint64 items);
    void updateClusterPrefs(quint64 items);
    void updateServerConfigs(quint64 items);

    void updateIfPref(prefIfItem pif);
    void updateColPref(prefColItem pra);
    void updateRaPref(prefRaItem pra);
    void updateRsPref(prefRsItem pra);
    void updateCtPref(prefCtItem pct);
    void updateLanPref(prefLanItem plan);
    void updateClusterPref(prefClusterItem pcl);
    void updateServerConfig(prefServerItem si);

    void updateUdpPref(prefUDPItem upi);
    void updateUdpPrefs(int items);


    void updateModSourceList(uchar num, QVector<rigInput> data);
    void setAudioDevicesUI();

signals:
    void changedIfPrefs(quint64 items);
    void changedColPrefs(quint64 items);
    void changedRaPrefs(quint64 items);
    void changedRsPrefs(quint64 items);
    void changedCtPrefs(quint64 items);
    void changedLanPrefs(quint64 items);
    void changedClusterPrefs(quint64 items);
    void changedUdpPrefs(quint64 items);
    void changedServerPrefs(quint64 items);

    void changedAudioOutputCombo(int index);
    void changedAudioInputCombo(int index);
    void changedServerTXAudioOutputCombo(int index);
    void changedServerRXAudioInputCombo(int index);

    void changedIfPref(prefIfItem i);
    void changedColPref(prefColItem i);
    void changedRaPref(prefRaItem i);
    void changedRsPref(prefRsItem i);
    void changedCtPref(prefCtItem i);
    void changedLanPref(prefLanItem i);
    void changedClusterPref(prefClusterItem i);
    void changedUdpPref(prefUDPItem i);
    void changedServerPref(prefServerItem i);

    void changedModInput(uchar num, inputTypes input);    

private slots:
    void on_settingsList_currentRowChanged(int currentRow);
    void onServerUserFieldChanged();
    void on_lanEnableBtn_clicked(bool checked);
    void on_autoSSBchk_clicked(bool checked);
    void on_useSystemThemeChk_clicked(bool checked);
    void on_enableUsbChk_clicked(bool checked);
    void on_usbControllersSetup_clicked();
    void on_usbControllersReset_clicked();
    void on_autoPollBtn_clicked(bool checked);
    void on_manualPollBtn_clicked(bool checked);
    void on_pollTimeMsSpin_valueChanged(int arg1);
    void on_serialDeviceListCombo_activated(const QString &arg1);
    void on_baudRateCombo_activated(int index);
    void on_vspCombo_activated(int index);
    void on_audioSystemCombo_currentIndexChanged(int index);
    void on_audioSystemServerCombo_currentIndexChanged(int index);
    void on_meter2selectionCombo_currentIndexChanged(int index);
    void on_tuningFloorZerosChk_clicked(bool checked);
    void on_fullScreenChk_clicked(bool checked);
    void on_wfAntiAliasChk_clicked(bool checked);
    void on_wfInterpolateChk_clicked(bool checked);
    void on_underlayNone_clicked(bool checked);
    void on_underlayPeakHold_clicked(bool checked);
    void on_underlayPeakBuffer_clicked(bool checked);
    void on_underlayAverageBuffer_clicked(bool checked);
    void on_underlayBufferSlider_valueChanged(int value);
    void on_pttEnableChk_clicked(bool checked);
    void on_rigCreatorChk_clicked(bool checked);
    void on_serialEnableBtn_clicked(bool checked);
    void on_rigCIVManualAddrChk_clicked(bool checked);
    void on_rigCIVaddrHexLine_editingFinished();
    void on_useCIVasRigIDChk_clicked(bool checked);
    void on_enableRigctldChk_clicked(bool checked);
    void on_rigctldPortTxt_editingFinished();
    void on_tcpServerPortTxt_editingFinished();
    void on_clusterServerNameCombo_currentIndexChanged(int index);
    void on_clusterUdpEnable_clicked(bool checked);
    void on_clusterTcpEnable_clicked(bool checked);
    void on_clusterTcpAddBtn_clicked();
    void on_clusterServerNameCombo_currentTextChanged(const QString &arg1);
    void on_clusterTcpPortLineEdit_editingFinished();
    void on_clusterUsernameLineEdit_editingFinished();
    void on_clusterPasswordLineEdit_editingFinished();
    void on_clusterTimeoutLineEdit_editingFinished();
    void on_clusterUdpPortLineEdit_editingFinished();
    void on_clusterSkimmerSpotsEnable_clicked(bool checked);
    void on_debugBtn_clicked();
    void on_ipAddressTxt_textChanged(const QString &arg1);
    void on_usernameTxt_textChanged(const QString &arg1);
    void on_controlPortTxt_textChanged(const QString &arg1);
    void on_passwordTxt_textChanged(const QString &arg1);
    void on_audioDuplexCombo_currentIndexChanged(int index);
    void on_audioOutputCombo_currentIndexChanged(int index);
    void on_audioInputCombo_currentIndexChanged(int index);

    void on_rxLatencySlider_valueChanged(int value);
    void on_txLatencySlider_valueChanged(int value);
    void on_audioRXCodecCombo_currentIndexChanged(int value);
    void on_audioTXCodecCombo_currentIndexChanged(int value);
    void on_audioSampleRateCombo_currentIndexChanged(int value);

    void on_modInputCombo_activated(int index);
    void on_modInputData1Combo_activated(int index);
    void on_modInputData2Combo_activated(int index);
    void on_modInputData3Combo_activated(int index);

    void on_useUTCChk_clicked(bool checked);
    void on_setClockBtn_clicked();
    void on_pttOnBtn_clicked();
    void on_pttOffBtn_clicked();
    void on_adjRefBtn_clicked();
    void on_satOpsBtn_clicked();

    void on_serverEnableCheckbox_clicked(bool checked);
    void on_serverRXAudioInputCombo_currentIndexChanged(int index);
    void on_serverTXAudioOutputCombo_currentIndexChanged(int index);
    void on_serverControlPortText_textChanged(QString text);
    void on_serverCivPortText_textChanged(QString text);
    void on_serverAudioPortText_textChanged(QString text);

    // Color slots
    void on_colorPresetCombo_currentIndexChanged(int index);

    void on_colorSetBtnPlotBackground_clicked();
    void on_colorEditPlotBackground_editingFinished();
    void on_colorSetBtnSpecLine_clicked();
    void on_colorEditSpecLine_editingFinished();
    void on_colorSetBtnGrid_clicked();
    void on_colorEditGrid_editingFinished();
    void on_colorSetBtnText_clicked();
    void on_colorEditText_editingFinished();
    void on_colorSetBtnSpecFill_clicked();
    void on_colorEditSpecFill_editingFinished();
    void on_colorSetBtnAxis_clicked();
    void on_colorEditAxis_editingFinished();
    void on_colorSetBtnUnderlayLine_clicked();
    void on_colorEditUnderlayLine_editingFinished();
    void on_colorSetBtnUnderlayFill_clicked();
    void on_colorEditUnderlayFill_editingFinished();
    void on_colorSetBtnwfBackground_clicked();
    void on_colorEditWfBackground_editingFinished();
    void on_colorSetBtnWfGrid_clicked();
    void on_colorEditWfGrid_editingFinished();
    void on_colorSetBtnWfAxis_clicked();
    void on_colorEditWfAxis_editingFinished();
    void on_colorSetBtnWfText_clicked();
    void on_colorEditWfText_editingFinished();
    void on_colorSetBtnTuningLine_clicked();
    void on_colorEditTuningLine_editingFinished();
    void on_colorSetBtnPassband_clicked();
    void on_colorEditPassband_editingFinished();
    void on_colorSetBtnPBT_clicked();
    void on_colorEditPBT_editingFinished();
    void on_colorSetBtnMeterLevel_clicked();
    void on_colorEditMeterLevel_editingFinished();
    void on_colorSetBtnMeterAvg_clicked();
    void on_colorEditMeterAvg_editingFinished();
    void on_colorSetBtnMeterScale_clicked();
    void on_colorEditMeterScale_editingFinished();
    void on_colorSetBtnMeterText_clicked();
    void on_colorEditMeterText_editingFinished();
    void on_colorSetBtnClusterSpots_clicked();
    void on_colorEditClusterSpots_editingFinished();
    void on_colorRenamePresetBtn_clicked();
    void on_colorRevertPresetBtn_clicked();
    void on_colorSetBtnMeterPeakLevel_clicked();
    void on_colorEditMeterPeakLevel_editingFinished();
    void on_colorSetBtnMeterPeakScale_clicked();
    void on_colorEditMeterPeakScale_editingFinished();

private:
    Ui::settingswidget *ui;
    void createSettingsListItems();
    void populateComboBoxes();
    void updateAllPrefs();
    void updateUnderlayMode();
    void setUItoClustersList();

    // Utility:
    void quietlyUpdateSlider(QSlider* sl, int val);
    void quietlyUpdateCombobox(QComboBox *cb, int index);
    void quietlyUpdateCombobox(QComboBox *cb, QVariant val);
    void quietlyUpdateCombobox(QComboBox *cb, QString val);
    void quietlyUpdateSpinbox(QSpinBox *sb, int val);
    void quietlyUpdateCheckbox(QCheckBox *cb, bool isChecked);
    void quietlyUpdateRadiobutton(QRadioButton *rb, bool isChecked);
    void quietlyUpdateLineEdit(QLineEdit *le, const QString text);

    // Colors (helper functions)
    void setEditAndLedFromColor(QColor c, QLineEdit *e, QLedLabel *d);
    void setColorButtonOperations(QColor *colorStore, QLineEdit *e, QLedLabel *d);
    void setColorLineEditOperations(QColor *colorStore, QLineEdit *e, QLedLabel *d);
    void loadColorPresetToUIandPlots(int presetNumber);
    void setColorElement(QColor color, QLedLabel *led, QLabel *label);
    void setColorElement(QColor color, QLedLabel *led, QLineEdit *lineText);
    void setColorElement(QColor color, QLedLabel *led, QLabel *label, QLineEdit *lineText);
    QColor getColorFromPicker(QColor initialColor);
    void getSetColor(QLedLabel *led, QLabel *label);
    void getSetColor(QLedLabel *led, QLineEdit *line);
    QString setColorFromString(QString aarrggbb, QLedLabel *led);

    void populateServerUsers();
    void serverAddUserLine(const QString& user, const QString& pass, const int& type);

    preferences *prefs = NULL;
    colorPrefsType *colorPreset;
    udpPreferences *udpPrefs = NULL;
    SERVERCONFIG *serverConfig = NULL;
    bool havePrefs = false;
    bool haveUdpPrefs = false;
    bool haveServerConfig = false;
    bool haveSerialDevices = false;
    bool haveVspDevices = false;
    bool haveAudioInputs = false;
    bool haveAudioOutputs = false;
    bool haveServerAudioInputs = false;
    bool haveServerAudioOutputs = false;
    bool haveClusterList = false;
    bool updatingUIFromPrefs = false;

    QList<clusterSettings> clusters;

    audioDevices* audioDev = Q_NULLPTR;

};

#endif // SETTINGSWIDGET_H
