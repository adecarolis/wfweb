#ifndef MEMORIES_H
#define MEMORIES_H

#include <QDialog>
#include <QItemDelegate>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QPushButton>

#include "tablewidget.h"
#include "wfviewtypes.h"
#include "rigidentities.h"

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
    void getMemory(quint16 mem);
    void recallMemory(quint16 mem);
    void clearMemory(quint16 mem);
    void blockUpdates(bool block);


private slots:
    void on_table_cellChanged(int row, int col);
    void receiveMemory(memoryType mem);
    void rowAdded(int row);
    void rowDeleted(quint16 mem);

private:
    int currentRow=-1;
    memoryType* currentMemory = Q_NULLPTR;
    QStandardItemModel* createModel(QStandardItemModel* model, QStringList strings);

    QStringList memory;
    QStringList VFO;
    QStringList duplexModes;
    QStringList modes;
    QStringList dataModes;
    QStringList filters;
    QStringList tones;
    QStringList toneModes;

    QStandardItemModel* memoryModel = Q_NULLPTR;
    QStandardItemModel* vfoModel = Q_NULLPTR;
    QStandardItemModel* dataModel = Q_NULLPTR;
    QStandardItemModel* filterModel = Q_NULLPTR;
    QStandardItemModel* toneModesModel = Q_NULLPTR;
    QStandardItemModel* tonesModel = Q_NULLPTR;
    QStandardItemModel* tsqlModel = Q_NULLPTR;
    QStandardItemModel* modesModel = Q_NULLPTR;

    tableCombobox* memoryList = Q_NULLPTR;
    tableCombobox* vfoList = Q_NULLPTR;
    tableCombobox* dataList = Q_NULLPTR;
    tableCombobox* filterList = Q_NULLPTR;
    tableCombobox* toneModesList = Q_NULLPTR;
    tableCombobox* tonesList = Q_NULLPTR;
    tableCombobox* tsqlList = Q_NULLPTR;
    tableCombobox* modesList = Q_NULLPTR;

    rigCapabilities rigCaps;

    Ui::memories *ui;
    bool extended = false;
    enum columns {
        //  0             1         2       3         4           5           6           7          8        9           10        11
        //"Recall" << "Setting<< "Num" << "Name" << "Band" << "Channel" << "Mem" << "VFO" << "Frequency" << "Mode" << "Filter" << "Data" <<
        //    12           13          14         15        16        17          18       19      20     21
        // "Duplex" << "Tone Mode" << "DSQL" << "Tone" << "TSQL" << "DTCS" << "Offset" << "UR" << "R1" << "R2";
        columnRecall=0,
        columnNum,
        columnMemory,
        columnName,
        columnBand,
        columnChannel,
        columnMem,
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
        columnOffset,
        columnUR,
        columnR1,
        columnR2,
        totalColumns
    };
};

#endif // MEMORIES_H
