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
    void getMemory(quint32 mem);
    void recallMemory(quint32 mem);
    void clearMemory(quint32 mem);
    void memoryMode();
    void vfoMode();


private slots:
    void on_table_cellChanged(int row, int col);
    void on_group_currentIndexChanged(int index);
    void on_vfoMode_clicked();
    void on_memoryMode_clicked();
    void receiveMemory(memoryType mem);
    void rowAdded(int row);
    void rowDeleted(quint32 mem);

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
    QStringList dtsc;
    QStringList dtcsp;
    QStringList dsql;

    QStandardItemModel* memoryModel = Q_NULLPTR;
    QStandardItemModel* vfoModel = Q_NULLPTR;
    QStandardItemModel* filterModel = Q_NULLPTR;
    QStandardItemModel* dataModel = Q_NULLPTR;
    QStandardItemModel* duplexModel = Q_NULLPTR;
    QStandardItemModel* toneModesModel = Q_NULLPTR;
    QStandardItemModel* dsqlModel = Q_NULLPTR;
    QStandardItemModel* tonesModel = Q_NULLPTR;
    QStandardItemModel* tsqlModel = Q_NULLPTR;
    QStandardItemModel* dtcspModel = Q_NULLPTR;
    QStandardItemModel* modesModel = Q_NULLPTR;

    tableEditor* numEditor = Q_NULLPTR;
    tableCombobox* memoryList = Q_NULLPTR;
    tableEditor* nameEditor = Q_NULLPTR;
    tableCombobox* vfoList = Q_NULLPTR;
    tableEditor* freqEditor = Q_NULLPTR;
    tableCombobox* filterList = Q_NULLPTR;
    tableCombobox* dataList = Q_NULLPTR;
    tableCombobox* duplexList = Q_NULLPTR;
    tableCombobox* toneModesList = Q_NULLPTR;
    tableCombobox* dsqlList = Q_NULLPTR;
    tableCombobox* tonesList = Q_NULLPTR;
    tableCombobox* tsqlList = Q_NULLPTR;
    tableEditor* dtcsEditor = Q_NULLPTR;
    tableCombobox* dtcspList = Q_NULLPTR;
    tableCombobox* modesList = Q_NULLPTR;
    tableEditor* offsetEditor = Q_NULLPTR;
    tableEditor* dvsqlEditor = Q_NULLPTR;
    tableEditor* urEditor = Q_NULLPTR;
    tableEditor* r1Editor = Q_NULLPTR;
    tableEditor* r2Editor = Q_NULLPTR;

    rigCapabilities rigCaps;

    Ui::memories *ui;
    bool extended = false;
    enum columns {
        columnRecall=0,
        columnNum,
        columnMemory,
        columnName,
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
        totalColumns
    };
};

#endif // MEMORIES_H
