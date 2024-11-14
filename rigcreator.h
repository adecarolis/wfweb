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
    void changed();


private:
    Ui::rigCreator *ui;
    void closeEvent(QCloseEvent *event);
    QMenu* context;
    tableCombobox* commandsList;
    tableCombobox* priorityList;
    QStandardItemModel* commandsModel;
    QStandardItemModel* command36Model;
    QStandardItemModel* priorityModel;
    QStandardItemModel* createModel(int num, QStandardItemModel* model, QString strings[]);
    QStandardItemModel* createModel(QStandardItemModel* model, QStringList strings);

    QString currentFile;
    bool settingsChanged=false;

};



#endif // RIGCREATOR_H
