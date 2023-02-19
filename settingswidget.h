#ifndef SETTINGSWIDGET_H
#define SETTINGSWIDGET_H

#include <QWidget>
#include <QSlider>
#include <QCheckBox>
#include <QRadioButton>

#include "logcategories.h"
#include "prefs.h"
#include "colorprefs.h"
#include "udpbase.h" // for udp preferences

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

    void updateIfPrefs(int items);
    void updateRaPrefs(int items);
    void updateCtPrefs(int items);
    void updateLanPrefs(int items);
    void updateClusterPrefs(int items);

    void updateIfPref(prefIfItem pif);
    void updateRaPref(prefRaItem pra);
    void updateCtPref(prefCtItem pct);
    void updateLanPref(prefLanItem plan);
    void updateClusterPref(prefClusterItem pcl);

    // depreciated:
    //void externalChangedPreferences(prefItem pi);
    void updateUdpPref(udpPrefsItem upi);
    //void externalChangedMultiplePreferences(uint64_t items);
    void updateUdpPrefs(int items);


signals:
    // Not sure if we should do it this way,
    // since many of these changes are not thought
    // of as merely "preferences"... although they generally are...
    // ...hmm
    void changedIfPref(int items);
    void changedRaPref(int items);
    void changedCtPrefs(int items);
    void changedLanPrefs(int items);
    void changedClusterPrefs(int items);

private slots:
    void on_settingsList_currentRowChanged(int currentRow);

    void on_lanEnableBtn_clicked(bool checked);

private:
    Ui::settingswidget *ui;
    void createSettingsListItems();
    void populateComboBoxes();
    void updateAllPrefs();
    void updateUnderlayMode();
    void quietlyUpdateSlider(QSlider* sl, int val);
    void quietlyUpdateCheckbox(QCheckBox *cb, bool isChecked);
    void quietlyUpdateRadiobutton(QRadioButton *rb, bool isChecked);

    preferences *prefs = NULL;
    udpPreferences *udpPrefs = NULL;
    bool havePrefs = false;
    bool haveUdpPrefs = false;
    bool updatingUIFromPrefs = false;
};

#endif // SETTINGSWIDGET_H
