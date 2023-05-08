#ifndef MEMORIES_H
#define MEMORIES_H

#include <QDialog>
#include <QItemDelegate>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QPushButton>
#include <QFileDialog>
#include <QTimer>
#include <QMessageBox>

#include "tablewidget.h"
#include "wfviewtypes.h"
#include "rigidentities.h"

#define MEMORY_TIMEOUT 1000

namespace Ui {
class memories;
}

class memories : public QDialog
{
    Q_OBJECT

public:
    explicit memories(rigCapabilities rigCaps,QWidget *parent = nullptr);
    ~memories();
    void populate();

signals:
    void setMemory(memoryType mem);
    void getMemory(quint32 mem);
    void setSatelliteMode(bool en);
    void getSatMemory(quint32 mem);
    void recallMemory(quint32 mem);
    void clearMemory(quint32 mem);
    void memoryMode();
    void vfoMode();
    void setBand(char band);


private slots:
    void on_table_cellChanged(int row, int col);
    void on_group_currentIndexChanged(int index);
    void on_vfoMode_clicked();
    void on_memoryMode_clicked();
    void on_csvImport_clicked();
    void on_csvExport_clicked();
    bool readCSVRow (QTextStream &in, QStringList *row);


    void receiveMemory(memoryType mem);
    void rowAdded(int row);
    void rowDeleted(quint32 mem);

    void timeout();

private:
    int currentRow=-1;
    memoryType* currentMemory = Q_NULLPTR;
    int groupMemories=0;
    quint32 lastMemoryRequested=0;
    QTimer timeoutTimer;
    int timeoutCount=0;

    QStandardItemModel* createModel(QStandardItemModel* model, QStringList strings);

    QStringList split;
    QStringList scan;
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


    tableEditor* numEditor = Q_NULLPTR;
    tableCombobox* splitList = Q_NULLPTR;
    tableCombobox* scanList = Q_NULLPTR;
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
    tableEditor* dvsqlEditor = Q_NULLPTR;
    tableEditor* urEditor = Q_NULLPTR;
    tableEditor* r1Editor = Q_NULLPTR;
    tableEditor* r2Editor = Q_NULLPTR;

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
    tableEditor* dvsqlEditorB = Q_NULLPTR;
    tableEditor* urEditorB = Q_NULLPTR;
    tableEditor* r1EditorB = Q_NULLPTR;
    tableEditor* r2EditorB = Q_NULLPTR;

    rigCapabilities rigCaps;

    Ui::memories *ui;
    bool extended = false;

    enum columns {
        columnRecall=0,
        columnNum,
        columnName,
        columnSplit,
        columnScan,
        columnVFO,
        columnFrequency,
        columnMode,
        columnFilter,
        columnData,
        columnDuplex,
        columnToneMode,
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
};

#endif // MEMORIES_H
