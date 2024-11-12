#ifndef MEMORIES_H
#define MEMORIES_H

#include <QWidget>
#include <QItemDelegate>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QPushButton>
#include <QFileDialog>
#include <QTimer>
#include <QMessageBox>
#include <QStatusBar>
#include <QProgressBar>

#include "tablewidget.h"
#include "wfviewtypes.h"
#include "rigidentities.h"
#include "cachingqueue.h"

#define MEMORY_TIMEOUT 1000
#define MEMORY_SLOWLOAD 500
#define MEMORY_SATGROUP 200
#define MEMORY_SHORTGROUP 100

namespace Ui {
class memories;
}

class memories : public QWidget
{
    Q_OBJECT

public:
    explicit memories(bool isAdmin, bool slowLoad=false,QWidget *parent = nullptr);
    ~memories();
    void populate();

private slots:
    void on_table_cellChanged(int row, int col);
    void on_group_currentIndexChanged(int index);
    void on_modeButton_clicked(bool on);
    void on_csvImport_clicked();
    void on_csvExport_clicked();
    void on_scanButton_toggled(bool scan);
    void on_disableEditing_toggled(bool dis);
    bool readCSVRow (QTextStream &in, QStringList *row);


    void receiveMemory(memoryType mem);
    void rowAdded(int row);
    void rowDeleted(quint32 mem);

    void timeout();

private:
    cachingQueue* queue;
    quint32 groupMemories=0;
    quint32 lastMemoryRequested=0;
    QTimer timeoutTimer;
    int timeoutCount=0;
    int retries=0;
    int visibleColumns=1;
    bool slowLoad=false;
    bool extendedView = false;

    bool checkASCII(QString str);

    QStandardItemModel* createModel(QStandardItemModel* model, QStringList strings);

    QStringList split;
    QStringList scan;
    QStringList skip;
    QStringList vfos;
    QStringList duplexModes;
    QStringList modes;
    QStringList dataModes;
    QStringList filters;
    QStringList tones;
    QStringList toneModes;
    QStringList dtcs;
    QStringList dtcsp;
    QStringList dsql;
    QStringList dvsql;
    QStringList tuningSteps;
    QStringList preamps;
    QStringList attenuators;
    QStringList antennas;
    QStringList ipplus;
    QStringList p25Sql;
    QStringList dPmrSql;
    QStringList dPmrSCRM;
    QStringList nxdnSql;
    QStringList nxdnEnc;
    QStringList dcrSql;
    QStringList dcrEnc;


    /*
        columnFrequencyB,
        columnModeB,
        columnFilterB,
        columnDataB,
        columnToneModeB,
        columnDSQLB,
        columnToneB,
        columnTSQLB,
        columnDTCSPolarityB,
        columnDTCSB,
        columnDVSquelchB,
        columnURB,
        columnR1B,
        columnR2B,
    */
    QStandardItemModel* splitModel = Q_NULLPTR;
    QStandardItemModel* skipModel = Q_NULLPTR;
    QStandardItemModel* scanModel = Q_NULLPTR;
    QStandardItemModel* filterModel = Q_NULLPTR;
    QStandardItemModel* vfoModel = Q_NULLPTR;
    QStandardItemModel* dataModel = Q_NULLPTR;
    QStandardItemModel* modesModel = Q_NULLPTR;
    QStandardItemModel* duplexModel = Q_NULLPTR;
    QStandardItemModel* toneModesModel = Q_NULLPTR;
    QStandardItemModel* dsqlModel = Q_NULLPTR;
    QStandardItemModel* tonesModel = Q_NULLPTR;
    QStandardItemModel* tsqlModel = Q_NULLPTR;
    QStandardItemModel* dtcspModel = Q_NULLPTR;
    QStandardItemModel* dtcsModel = Q_NULLPTR;
    QStandardItemModel* dvsqlModel = Q_NULLPTR;
    QStandardItemModel* tuningStepsModel = Q_NULLPTR;
    QStandardItemModel* preampsModel = Q_NULLPTR;
    QStandardItemModel* attenuatorsModel = Q_NULLPTR;
    QStandardItemModel* antennasModel = Q_NULLPTR;
    QStandardItemModel* ipplusModel = Q_NULLPTR;
    QStandardItemModel* p25SqlModel = Q_NULLPTR;
    QStandardItemModel* dPmrSqlModel = Q_NULLPTR;
    QStandardItemModel* dPmrSCRMModel = Q_NULLPTR;
    QStandardItemModel* nxdnSqlModel = Q_NULLPTR;
    QStandardItemModel* nxdnEncModel = Q_NULLPTR;
    QStandardItemModel* dcrSqlModel = Q_NULLPTR;
    QStandardItemModel* dcrEncModel = Q_NULLPTR;

    QStandardItemModel* vfoModelB = Q_NULLPTR;
    QStandardItemModel* modesModelB = Q_NULLPTR;
    QStandardItemModel* filterModelB = Q_NULLPTR;
    QStandardItemModel* dataModelB = Q_NULLPTR;
    QStandardItemModel* toneModesModelB = Q_NULLPTR;
    QStandardItemModel* dsqlModelB = Q_NULLPTR;
    QStandardItemModel* tonesModelB = Q_NULLPTR;
    QStandardItemModel* tsqlModelB = Q_NULLPTR;
    QStandardItemModel* dtcspModelB = Q_NULLPTR;
    QStandardItemModel* duplexModelB = Q_NULLPTR;
    QStandardItemModel* dtcsModelB = Q_NULLPTR;
    QStandardItemModel* dvsqlModelB = Q_NULLPTR;

    tableEditor* numEditor = Q_NULLPTR;
    tableCombobox* splitList = Q_NULLPTR;
    tableCombobox* scanList = Q_NULLPTR;
    tableCombobox* skipList = Q_NULLPTR;
    tableCombobox* vfoList = Q_NULLPTR;
    tableEditor* nameEditor = Q_NULLPTR;
    tableEditor* freqEditor = Q_NULLPTR;
    tableCombobox* filterList = Q_NULLPTR;
    tableCombobox* dataList = Q_NULLPTR;
    tableCombobox* duplexList = Q_NULLPTR;
    tableCombobox* toneModesList = Q_NULLPTR;
    tableCombobox* dsqlList = Q_NULLPTR;
    tableCombobox* tonesList = Q_NULLPTR;
    tableCombobox* tsqlList = Q_NULLPTR;
    tableCombobox* dtcsList = Q_NULLPTR;
    tableCombobox* dtcspList = Q_NULLPTR;
    tableCombobox* modesList = Q_NULLPTR;
    tableEditor* offsetEditor = Q_NULLPTR;
    tableCombobox* dvsqlList = Q_NULLPTR;
    tableEditor* urEditor = Q_NULLPTR;
    tableEditor* r1Editor = Q_NULLPTR;
    tableEditor* r2Editor = Q_NULLPTR;
    tableCombobox* tuningStepsList = Q_NULLPTR;
    tableEditor* tuningStepEditor = Q_NULLPTR;
    tableCombobox* preampsList = Q_NULLPTR;
    tableCombobox* attenuatorsList = Q_NULLPTR;
    tableCombobox* antennasList = Q_NULLPTR;
    tableCombobox* ipplusList = Q_NULLPTR;

    tableCombobox* p25SqlList = Q_NULLPTR;
    tableEditor* p25NacEditor = Q_NULLPTR;
    tableCombobox* dPmrSqlList = Q_NULLPTR;
    tableEditor* dPmrComIdEditor = Q_NULLPTR;
    tableEditor* dPmrCcEditor = Q_NULLPTR;
    tableCombobox* dPmrSCRMList = Q_NULLPTR;
    tableEditor* dPmrKeyEditor = Q_NULLPTR;
    tableCombobox* nxdnSqlList = Q_NULLPTR;
    tableEditor* nxdnRanEditor = Q_NULLPTR;
    tableCombobox* nxdnEncList = Q_NULLPTR;
    tableEditor* nxdnKeyEditor = Q_NULLPTR;
    tableCombobox* dcrSqlList = Q_NULLPTR;
    tableEditor* dcrUcEditor = Q_NULLPTR;
    tableCombobox* dcrEncList = Q_NULLPTR;
    tableEditor* dcrKeyEditor = Q_NULLPTR;

    tableCombobox* vfoListB = Q_NULLPTR;
    tableEditor* freqEditorB = Q_NULLPTR;
    tableCombobox* filterListB = Q_NULLPTR;
    tableCombobox* dataListB = Q_NULLPTR;
    tableCombobox* toneModesListB = Q_NULLPTR;
    tableCombobox* dsqlListB = Q_NULLPTR;
    tableCombobox* tonesListB = Q_NULLPTR;
    tableCombobox* tsqlListB = Q_NULLPTR;
    tableCombobox* dtcsListB = Q_NULLPTR;
    tableCombobox* dtcspListB = Q_NULLPTR;
    tableCombobox* modesListB = Q_NULLPTR;
    tableCombobox* duplexListB = Q_NULLPTR;
    tableEditor* offsetEditorB = Q_NULLPTR;
    tableCombobox* dvsqlListB = Q_NULLPTR;
    tableEditor* urEditorB = Q_NULLPTR;
    tableEditor* r1EditorB = Q_NULLPTR;
    tableEditor* r2EditorB = Q_NULLPTR;
    tableCombobox* tuningStepsListB = Q_NULLPTR;

    rigCapabilities* rigCaps = Q_NULLPTR;

    QStatusBar* statusBar;
    QProgressBar* progress;

    Ui::memories *ui;
    bool extended = false;

    enum columns {
        columnRecall=0,
        columnNum,
        columnName,
        columnSplit,
        columnSkip,
        columnScan,
        columnVFO,
        columnFrequency,
        columnMode,
        columnFilter,
        columnData,
        columnDuplex,
        columnToneMode,
        columnTuningStep,
        columnCustomTuningStep,
        columnAttenuator,
        columnPreamplifier,
        columnAntenna,
        columnIPPlus,
        columnDSQL,
        columnTone,
        columnTSQL,
        columnDTCS,
        columnDTCSPolarity,
        columnDVSquelch,
        columnOffset,
        columnUR,
        columnR1,
        columnR2,
        columnP25Sql,
        columnP25Nac,
        columnDPmrSql,
        columnDPmrComid,
        columnDPmrCc,
        columnDPmrSCRM,
        columnDPmrKey,
        columnNxdnSql,
        columnNxdnRan,
        columnNxdnEnc,
        columnNxdnKey,
        columnDcrSql,
        columnDcrUc,
        columnDcrEnc,
        columnDcrKey,
        columnVFOB,
        columnFrequencyB,
        columnModeB,
        columnFilterB,
        columnDataB,
        columnDuplexB,
        columnToneModeB,
        columnDSQLB,
        columnToneB,
        columnTSQLB,
        columnDTCSB,
        columnDTCSPolarityB,
        columnDVSquelchB,
        columnOffsetB,
        columnURB,
        columnR1B,
        columnR2B,
        totalColumns        
    };

    int updateCombo(QStringList& combo, int row, columns column, quint8 data);
    int updateCombo(QStringList& combo, int row, columns column, QString data);

    struct stepType {
        stepType(){};
        stepType(quint8 num, QString name, quint64 hz) : num(num), name(name), hz(hz) {};
        quint8 num;
        QString name;
        quint64 hz;
    };

    struct commandList {
        commandList(){};
        commandList(queuePriority prio, funcs func, uchar receiver) : prio(prio),func(func), receiver(receiver) {};
        queuePriority prio;
        funcs func;
        uchar receiver;
    };

    QList<commandList> activeCommands;
    QList<funcs> disabledCommands{
        funcMainFreq,funcSubFreq,funcMainMode,funcSubMode,funcPBTInner,funcPBTOuter,
        funcAttenuator, funcPreamp, funcAntenna, funcIPPlus, funcFilterWidth
    };

    void enableCell(int col, int row, bool en);
    void configColumns(int row, modeInfo mode);
};

#endif // MEMORIES_H
