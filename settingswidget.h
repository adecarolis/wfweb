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
    void updateServerConfig(serverItems si);

    void updateUdpPref(udpPrefsItem upi);
    void updateUdpPrefs(int items);

    void updateAudioInputs(QStringList deviceList, int currentIndex, int chars);
    void updateAudioOutputs(QStringList deviceList, int currentIndex, int chars);
    void updateServerRXAudioInputs(QStringList deviceList, int currentIndex, int chars);
    void updateServerTXAudioOutputs(QStringList deviceList, int currentIndex, int chars);

    void updateSerialPortList(QStringList deviceList, QVector<int> data);
    void updateVSPList(QStringList deviceList, QVector<int> data);

    void updateModSourceList(uchar num, QVector<rigInput> data);

signals:
    void changedIfPrefs(quint64 items);
    void changedRaPrefs(quint64 items);
    void changedRsPrefs(quint64 items);
    void changedCtPrefs(quint64 items);
    void changedLanPrefs(quint64 items);
    void changedClusterPrefs(quint64 items);
    void changedUdpPrefs(quint64 items);
    void changedServerConfigs(quint64 items);

    void changedAudioOutputCombo(int index);
    void changedAudioInputCombo(int index);
    void changedServerTXAudioOutputCombo(int index);
    void changedServerRXAudioInputCombo(int index);

    void changedIfPref(prefIfItem i);
    void changedRaPref(prefRaItem i);
    void changedRsPref(prefRsItem i);
    void changedCtPref(prefCtItem i);
    void changedLanPref(prefLanItem i);
    void changedClusterPref(prefClusterItem i);
    void changedUdpPref(udpPrefsItem i);
    void changedServerConfig(serverItems i);

    void showUSBControllerSetup();
    void changedModInput(uchar num, inputTypes input);

private slots:
    void on_settingsList_currentRowChanged(int currentRow);
    void onServerUserFieldChanged();
    void on_lanEnableBtn_clicked(bool checked);
    void on_autoSSBchk_clicked(bool checked);
    void on_useSystemThemeChk_clicked(bool checked);
    void on_enableUsbChk_clicked(bool checked);
    void on_usbControllerBtn_clicked();
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
    void on_clickDragTuningEnableChk_clicked(bool checked);
    void on_serialEnableBtn_clicked(bool checked);
    void on_enableRigctldChk_clicked(bool checked);
    void on_rigctldPortTxt_editingFinished();
    void on_tcpServerPortTxt_editingFinished();
    void on_clusterServerNameCombo_currentIndexChanged(int index);
    void on_clusterUdpEnable_clicked(bool checked);
    void on_clusterTcpEnable_clicked(bool checked);
    void on_clusterTcpSetNowBtn_clicked();
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
    void on_serverRXAudioInputCombo_currentIndexChanged(int index);
    void on_serverTXAudioOutputCombo_currentIndexChanged(int index);
    void on_serverEnableCheckbox_clicked(bool checked);

    void on_serverAddUserBtn_clicked();

    void on_modInputCombo_activated(int index);
    void on_modInputData1Combo_activated(int index);
    void on_modInputData2Combo_activated(int index);
    void on_modInputData3Combo_activated(int index);


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
    void quietlyUpdateSpinbox(QSpinBox *sb, int val);
    void quietlyUpdateCheckbox(QCheckBox *cb, bool isChecked);
    void quietlyUpdateRadiobutton(QRadioButton *rb, bool isChecked);
    void quietlyUpdateLineEdit(QLineEdit *le, const QString text);

    void populateServerUsers();
    void serverAddUserLine(const QString& user, const QString& pass, const int& type);

    preferences *prefs = NULL;
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

};

#endif // SETTINGSWIDGET_H
