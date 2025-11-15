#ifndef RIGCREATOR_H
#define RIGCREATOR_H

#include <QDialog>
#include <QItemDelegate>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
#include <QHeaderView>
#include <QStandardPaths>
#include <QColorDialog>
#include "wfviewtypes.h"
#include "rigidentities.h"
#include "tablewidget.h"
#include "cachingqueue.h"


inline QList<periodicType> defaultPeriodic = {
    {funcFreq,"Medium",0},
    {funcMode,"Medium",0},
    {funcOverflowStatus,"Medium",0},
    {funcScopeMode,"Medium High",0},
    {funcScopeSpan,"Medium High",0},
    {funcScopeSingleDual,"Medium High",0},
    {funcScopeMainSub,"Medium High",0},
    {funcScopeSpeed,"Medium",0},
    {funcScopeHold,"Medium",0},
    {funcVFODualWatch,"Medium",0},
    {funcTransceiverStatus,"High",0},
    {funcDATAOffMod,"Medium High",0},
    {funcDATA1Mod,"Medium High",0},
    {funcDATA2Mod,"Medium High",0},
    {funcDATA3Mod,"Medium High",0},
    {funcRFPower,"Medium",0},
    {funcMonitorGain,"Medium Low",0},
    {funcMonitor,"Medium Low",0},
    {funcRfGain,"Medium",0},
    {funcTunerStatus,"Medium",0},
    {funcTuningStep,"Medium Low",0},
    {funcAttenuator,"Medium Low",0},
    {funcPreamp,"Medium Low",0},
    {funcAntenna,"Medium Low",0},
    {funcToneSquelchType,"Medium Low",0},
    {funcSMeter,"Highest",0}
};

namespace Ui {
class rigCreator;
}

class rigCreator : public QDialog
{
    Q_OBJECT

public:
    explicit rigCreator(QWidget *parent = nullptr);
    ~rigCreator();

private slots:
    void on_loadFile_clicked(bool clicked);
    void on_saveFile_clicked(bool clicked);
    void on_hasCommand29_toggled(bool checked);
    void on_defaultRigs_clicked(bool clicked);
    void loadRigFile(QString filename);
    void saveRigFile(QString filename);
    void commandRowAdded(int row);
    void bandRowAdded(int row);
    void meterRowAdded(int row);
    void changed();


private:
    Ui::rigCreator *ui;
    void closeEvent(QCloseEvent *event);
    QMenu* context;
    tableCombobox* commandsList;
    tableCombobox* metersList;
    tableCombobox* priorityList;
    QStandardItemModel* commandsModel;
    QStandardItemModel* metersModel;
    QStandardItemModel* command36Model;
    QStandardItemModel* priorityModel;
    QStandardItemModel* createModel(int num, QStandardItemModel* model, QString strings[]);
    QStandardItemModel* createModel(QStandardItemModel* model, QStringList strings);

    QString currentFile;
    bool settingsChanged=false;

    const float kenwoodTones[42]{67.0f,69.3f,71.9f,74.4f,77.0f,79.7f,82.5f,85.4f,88.5f,91.5f,95.8f,97.4f,100.0f,103.5f,107.2f,110.9f,
                                 114.8f,118.8f,123.0f,127.3f,131.8f,136.5f,141.3f,146.2f,151.4f,156.7f,162.2f,167.9f,173.8f,179.9f,
                                 186.2f,192.8f,203.5f,206.5f,210.7f,218.1f,226.7f,229.1f,233.6f,241.8f,250.3f,254.1f};

    const float yaesuTones[50]{67.0f,69.3f,71.9f,74.4f,77.0f,79.7f,82.5f,85.4f,88.5f,91.5f,94.8f,97.4f,100.0f,103.5f,107.2f,110.9f,
                                 114.8f,118.8f,123.0f,127.3f,131.8f,136.5f,141.3f,146.2f,151.4f,156.7f,159.8f,162.2f,165.5f,167.9f,171.3f,173.8f,177.3f,179.9f,
                                 183.5f,186.2f,189.9f,192.8f,196.6f,199.5f,203.5f,206.5f,210.7f,218.1f,226.7f,229.1f,233.6f,241.8f,250.3f,254.1f};


    const float icomTones[48]{67.0f,69.3f,71.9f,74.4f,77.0f,79.7f,82.5f,85.4f,88.5f,91.5f,94.8f,97.4f,100.0f,103.5f,107.2f,110.9f,
                              114.8f,118.8f,123.0f,127.3f,131.8f,136.5f,141.3f,146.2f,151.4f,156.7f,159.8f,162.2f,165.5f,167.9f,173.8f,177.3f,179.9f,
                              183.5f,186.2f,192.8f,196.6f,199.5f,203.5f,206.5f,210.7f,218.1f,225.7f,229.1f,233.6f,241.8f,250.3f,254.1f};

    const short icomDtcs[104]{23,25,26,31,32,36,43,47,51,53,54,65,71,72,73,74,114,115,116,122,125,131,132,134,143,145,152,155,156,162,165,172,174,
                            205,212,223,225,226,243,244,245,246,251,252,255,261,263,265,266,271,274,306,311,315,325,331,332,343,346,351,356,364,365,371,
                            411,412,413,423,431,432,445,446,452,454,455,462,464,465,466,503,506,516,523,526,532,546,565,
                            606,612,624,627,631,632,654,662,664,703,712,723,731,732,734,746,754};
};



#endif // RIGCREATOR_H
