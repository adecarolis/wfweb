#include <QDebug>
#include "logcategories.h"

#include "memories.h"
#include "ui_memories.h"

memories::memories(rigCapabilities rigCaps, QWidget *parent) :
    QDialog(parent),
    rigCaps(rigCaps),
    ui(new Ui::memories)
{
    ui->setupUi(this);
    ui->table->setColumnCount(totalColumns);
    QStringList headers;
    headers << "" << "Num" << "Memory" << "Name" << "Band" << "Channel" << "Mem" << "VFO" << "Frequency" << "Mode" <<
        "Filter" << "Data" << "Duplex" << "Tone Mode" << "DSQL" << "Tone" << "TSQL" << "DTCS" << "Offset" << "UR" << "R1" << "R2";
    ui->table->setHorizontalHeaderLabels(headers);
    //  0             1         2       3         4           5           6           7          8        9           10        11
    //"Recall" << "Setting<< "Num" << "Name" << "Band" << "Channel" << "Mem" << "VFO" << "Frequency" << "Mode" << "Filter" << "Data" <<
    //    12           13          14         15        16        17          18       19      20     21
    // "Duplex" << "Tone Mode" << "DSQL" << "Tone" << "TSQL" << "DTCS" << "Offset" << "UR" << "R1" << "R2";

    if (!extended) {
        ui->table->hideColumn(columnBand);
        ui->table->hideColumn(columnChannel);
        ui->table->hideColumn(columnMem);
        ui->table->hideColumn(columnDuplex);
        ui->table->hideColumn(columnDSQL);
        ui->table->hideColumn(columnDTCS);
        ui->table->hideColumn(columnOffset);
        ui->table->hideColumn(columnUR);
        ui->table->hideColumn(columnR1);
        ui->table->hideColumn(columnR2);
    }

    if (rigCaps.memGroups < 2) {
        ui->groupLabel->hide();
        ui->group->hide();
    }

    ui->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    memory.append({"OFF","*1","*2","*3"});
    duplexModes.append({"OFF","DUP-","DUP+","RPS"});
    if (rigCaps.commands.contains(funcVFOEqualAB)) {
        VFO.append({"VFOA","VFOB"});
    } else if (rigCaps.commands.contains(funcVFOEqualMS)) {
        VFO.append({"Main","Sub"});
    }
    dataModes.append({"OFF","DATA1","DATA2","DATA3"});
    filters.append({"FIL1","FIL2","FIL3"});
    toneModes.append({"OFF","Tone","TSQL"});

    foreach (auto mode, rigCaps.modes){
        modes.append(mode.name);
    }

    tones.append({"67.0","69.3","71.9","74.4","77.0","79.7","82.5","85.4","88.5","91.5","94.8","97.4","100.0","103.5","107.2","110.9","114.8","118.8","123.0","127.3","131.8","136.5",
                    "141.3","146.2","151.4","156.7","159.8","162.2","165.5","167.9","171.3","173.8","177.3","179.9","183.5","186.2","189.9","192.8","196.6","199.5","203.5","206.5",
                    "210.7","218.1","225.7","229.1","233.6","241.8","250.3","254.1"});


    memoryList = new tableCombobox(createModel(memoryModel, memory),false,ui->table);
    ui->table->setItemDelegateForColumn(columnMemory, memoryList);


    vfoList = new tableCombobox(createModel(vfoModel, VFO),false,ui->table);
    ui->table->setItemDelegateForColumn(columnVFO, vfoList);

    dataList = new tableCombobox(createModel(dataModel, dataModes),false,ui->table);
    ui->table->setItemDelegateForColumn(columnData, dataList);

    filterList = new tableCombobox(createModel(filterModel, filters),false,ui->table);
    ui->table->setItemDelegateForColumn(columnFilter, filterList);

    toneModesList = new tableCombobox(createModel(toneModesModel, toneModes),false,ui->table);
    ui->table->setItemDelegateForColumn(columnToneMode, toneModesList);

    tonesList = new tableCombobox(createModel(tonesModel, tones),false,ui->table);
    ui->table->setItemDelegateForColumn(columnTone, tonesList);

    tsqlList = new tableCombobox(createModel(tsqlModel, tones),false,ui->table);
    ui->table->setItemDelegateForColumn(columnTSQL, tsqlList);

    modesList = new tableCombobox(createModel(modesModel, modes),false,ui->table);
    ui->table->setItemDelegateForColumn(columnMode, modesList);

    connect(ui->table,SIGNAL(rowAdded(int)),this,SLOT(rowAdded(int)));
    connect(ui->table,SIGNAL(rowDeleted(quint16)),this,SLOT(rowDeleted(quint16)));

    ui->table->sortByColumn(1,Qt::AscendingOrder);
}

void memories::populate()
{

    for (quint16 m=1;m<=rigCaps.memories;m++)
    {
        emit getMemory(m);
    }

}
memories::~memories()
{
    qInfo() << "Deleting memories table";
    ui->table->clear();
    delete ui;
}

void memories::rowAdded(int row)
{
    // Find the next available memory number:
    qInfo() << "Row Added" << row;
    quint16 prev=0;
    ui->table->blockSignals(true);
    for (int i=0;i < ui->table->rowCount();i++)
    {
        if (ui->table->item(i,columnNum) == NULL)
            continue;
        quint16 num = ui->table->item(i,columnNum)->text().toInt();
        if (num>prev+1)
        {
            // We have a gap;
            QPushButton* recall = new QPushButton("Recall");
            ui->table->setCellWidget(row,columnRecall,recall);
            connect(recall, &QPushButton::clicked, this,
                    [=]() { qInfo() << "Recalling" << prev+1; emit recallMemory(prev+1);});
            ui->table->model()->setData(ui->table->model()->index(row,columnNum),QString::number(prev+1).rightJustified(3,'0'));
            ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() | Qt::ItemIsEditable);
            break;
        }
        prev=num;
    }
    ui->table->blockSignals(false);
}

void memories::rowDeleted(quint16 mem)
{
    if (mem && mem <= rigCaps.memories) {
        qInfo() << "Mem Deleted" << mem;
        emit clearMemory(mem);
    }
}

void memories::on_table_cellChanged(int row, int col)
{
    Q_UNUSED(col)

    if (row != currentRow || currentMemory == Q_NULLPTR)
    {
        if (currentMemory != Q_NULLPTR)
        {
            delete currentMemory;
            currentMemory = Q_NULLPTR;
        }
        currentMemory = new memoryType();
        currentRow = row;
    }

    currentMemory->channel = (ui->table->item(row,columnNum) == NULL) ? 0 : ui->table->item(row,columnNum)->text().toInt();

    for (quint8 f=0;f<memory.size();f++)
    {
        if ((ui->table->item(row,columnMemory) == NULL) ? false : ui->table->item(row,columnMemory)->text()==memory[f]) {
            currentMemory->memory=f;
            break;
        }
    }

    strncpy_s(currentMemory->name,((ui->table->item(row,columnName) == NULL) ? "" : ui->table->item(row,columnName)->text()).toStdString().c_str(),10);

    for (quint8 f=0;f<VFO.size();f++)
    {
        if ((ui->table->item(row,columnVFO) == NULL) ? false : ui->table->item(row,columnVFO)->text()==VFO[f]) {
            currentMemory->frequency.VFO=(selVFO_t)f;
            break;
        }
    }

    currentMemory->frequency.Hz = (ui->table->item(row,columnFrequency) == NULL) ? 0 : ui->table->item(row,columnFrequency)->text().toLongLong();

    foreach (auto m, rigCaps.modes){
        if ((ui->table->item(row,columnMode) == NULL) ? false : ui->table->item(row,columnMode)->text()==m.name) {
            currentMemory->mode=m.mk;
            break;
        }
    }

    for (quint8 f=0;f<dataModes.size();f++)
    {
        if ((ui->table->item(row,columnData) == NULL) ? false : ui->table->item(row,columnData)->text()==dataModes[f]) {
            currentMemory->datamode=f;
            break;
        }
    }

    for (quint8 f=0;f<filters.size();f++)
    {
        if ((ui->table->item(row,columnFilter) == NULL) ? false : ui->table->item(row,columnFilter)->text()==filters[f]) {
            currentMemory->filter=f+1;
            break;
        }
    }

    for (quint8 f=0;f<toneModes.size();f++)
    {
        if ((ui->table->item(row,columnToneMode) == NULL) ? false : ui->table->item(row,columnToneMode)->text()==toneModes[f]) {
            currentMemory->tonemode=f;
            break;
        }
    }

    currentMemory->tone = (ui->table->item(row,columnTone) == NULL) ? 670 : int(ui->table->item(row,columnTone)->text().toFloat()*10.0);

    currentMemory->tsql = (ui->table->item(row,columnTSQL) == NULL) ? 670 : int(ui->table->item(row,columnTSQL)->text().toFloat()*10.0);

    // Only write the memory if ALL values are non-null
    bool write=true;
    for (int f=1; f<ui->table->columnCount();f++)
    {
        if (!ui->table->isColumnHidden(f) && (ui->table->item(row,f) == NULL))
            write=false;
    }
    if (write) {
        emit setMemory(*currentMemory);
        // Set number to not be editable once written. Not sure why but this crashes?
        //ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() & (~Qt::ItemIsEditable));
    }
}

void memories::receiveMemory(memoryType mem)
{
    if (mem.memory < 4) {
        ui->table->blockSignals(true);
        qInfo(logRig()) << "Received memory" << mem.channel << "Setting"  << mem.memory << "Name" << mem.name << "Freq" << mem.frequency.Hz << "Mode" << mem.mode;

        int row=-1;
        for (int n = 0; n<ui->table->rowCount();n++)
        {
            if (ui->table->item(n,0) != NULL && ui->table->item(n,0)->text().toInt() == mem.channel) {
                row = n;
                break;
            }
        }
        if (row == -1) {
            ui->table->insertRow(ui->table->rowCount());
            row=ui->table->rowCount()-1;
        }

        QPushButton* recall = new QPushButton("Recall");
        ui->table->setCellWidget(row,columnRecall,recall);
        connect(recall, &QPushButton::clicked, this,
                [=]() { qInfo() << "Recalling" << mem.channel; emit recallMemory(mem.channel);});

        ui->table->model()->setData(ui->table->model()->index(row,columnNum),QString::number(mem.channel).rightJustified(3,'0'));
        ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() & (~Qt::ItemIsEditable));
        for (int f=0;f<memory.size();f++)
        {
            if (f==mem.memory) {
                ui->table->model()->setData(ui->table->model()->index(row,columnMemory),memory[f]);
                break;
            }
        }
        ui->table->model()->setData(ui->table->model()->index(row,columnName),QString(mem.name).mid(0,10));
        for (int f=0;f<VFO.size();f++)
        {
            if (f==mem.frequency.VFO) {
                ui->table->model()->setData(ui->table->model()->index(row,columnVFO),VFO[f]);
                break;
            }
        }
        ui->table->model()->setData(ui->table->model()->index(row,columnFrequency),QString::number(mem.frequency.Hz));
        foreach (auto mode, rigCaps.modes){
            if (mode.reg == mem.mode) {
                ui->table->model()->setData(ui->table->model()->index(row,columnMode),mode.name);
                break;
            }
        }
        for (int f=0;f<dataModes.size();f++)
        {
            if (f==mem.datamode) {
                ui->table->model()->setData(ui->table->model()->index(row,columnData),dataModes[f]);
                break;
            }
        }

        for (int f=0;f<filters.size();f++)
        {
            if (f==mem.filter-1) {
                ui->table->model()->setData(ui->table->model()->index(row,columnFilter),filters[f]);
                break;
            }
        }
        for (int f=0;f<toneModes.size();f++)
        {
            if (f==mem.tonemode) {
                ui->table->model()->setData(ui->table->model()->index(row,columnToneMode),toneModes[f]);
                break;
            }
        }

        ui->table->model()->setData(ui->table->model()->index(row,columnTone),QString::number((float)mem.tone/10,'f',1));
        ui->table->model()->setData(ui->table->model()->index(row,columnTSQL),QString::number((float)mem.tsql/10,'f',1));
        ui->table->blockSignals(false);
    }
}

QStandardItemModel* memories::createModel(QStandardItemModel* model, QStringList strings)
{
    model = new QStandardItemModel();

    for (int i=0; i < strings.size();i++)
    {
        QStandardItem *itemName = new QStandardItem(strings[i]);
        QStandardItem *itemId = new QStandardItem(i);

        QList<QStandardItem*> row;
        row << itemName << itemId;

        model->appendRow(row);
    }

    return model;
}
