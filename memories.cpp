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

    /*
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
        columnOffset,
        columnUR,
        columnR1,
        columnR2,
     */

    headers << "" << "Num" << "Memory" << "Name" << "VFO" << "Frequency" << "Mode" <<
            "Filter" << "Data" << "Duplex" << "Tone Mode" << "DSQL" << "Tone" << "TSQL" << "DTCS" <<"DTCS Pol" << "DV Sql" << "Offset" << "UR" << "R1" << "R2";
    ui->table->setHorizontalHeaderLabels(headers);

    ui->groupLabel->hide();
    ui->group->hide();
    ui->vfoMode->hide();
    ui->memoryMode->hide();


    // Hide all columns except recall
    for (int i=1;i<ui->table->columnCount();i++)
    {
        ui->table->hideColumn(i);
    }

    foreach (auto parse, rigCaps.memParser) {
        switch (parse.spec)
        {
        case 'a':
            ui->groupLabel->show();
            ui->group->show();
            ui->vfoMode->show();
            ui->memoryMode->show();
            break;
        case 'b':
            ui->table->showColumn(columnNum);
            break;
        case 'c':
            ui->table->showColumn(columnMemory);
            break;
        case 'd':
            ui->table->showColumn(columnFrequency);
            break;
        case 'e':
            ui->table->showColumn(columnMode);
            break;
        case 'f':
            ui->table->showColumn(columnFilter);
            break;
        case 'g':
            ui->table->showColumn(columnData);
            break;
        case 'h':
            ui->table->showColumn(columnDuplex);
            ui->table->showColumn(columnToneMode);
            break;
        case 'i':
            ui->table->showColumn(columnData);
            ui->table->showColumn(columnToneMode);
            break;
        case 'j':
            ui->table->showColumn(columnDSQL);
            break;
        case 'k':
            ui->table->showColumn(columnTone);
            break;
        case 'l':
            ui->table->showColumn(columnTSQL);
            break;
        case 'm':
            ui->table->showColumn(columnDTCSPolarity);
            break;
        case 'n':
            ui->table->showColumn(columnDTCS);
            break;
        case 'o':
            ui->table->showColumn(columnDVSquelch);
            break;
        case 'p':
            ui->table->showColumn(columnOffset);
            break;
        case 'q':
            ui->table->showColumn(columnUR);
            break;
        case 'r':
            ui->table->showColumn(columnR1);
            break;
        case 's':
            ui->table->showColumn(columnR2);
            break;
        case 't':
            ui->table->showColumn(columnName);
            break;
        case 'u':
            break;
        case 'v':
            break;
        case 'w':
            break;
        case 'x':
            break;
        case 'y':
            break;
        case 'z':
            break;
        default:
            break;
        }
    }


    ui->group->blockSignals(true);
    for (int i=1;i<=rigCaps.memGroups;i++) {

        ui->group->addItem(QString("Group %0").arg(i,2,10,QChar('0')));
    }
    ui->group->setCurrentIndex(-1);
    ui->group->blockSignals(false);

    ui->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    memory = QStringList({"OFF","*1","*2","*3"});

    dataModes = QStringList({"OFF","DATA1"});
    if (rigCaps.commands.contains(funcDATA2Mod))
        dataModes.append("DATA2");
    if (rigCaps.commands.contains(funcDATA3Mod))
        dataModes.append("DATA3");

    filters = QStringList({"FIL1","FIL2","FIL3"});

    duplexModes = QStringList({"OFF","DUP-","DUP+","RPS"});

    toneModes = QStringList({"OFF","Tone","TSQL"});
    if (rigCaps.commands.contains(funcRepeaterDTCS))
        toneModes.append("DTCS");

    tones = QStringList({"67.0","69.3","71.9","74.4","77.0","79.7","82.5","85.4","88.5","91.5","94.8","97.4","100.0","103.5","107.2","110.9","114.8","118.8","123.0","127.3","131.8","136.5",
                  "141.3","146.2","151.4","156.7","159.8","162.2","165.5","167.9","171.3","173.8","177.3","179.9","183.5","186.2","189.9","192.8","196.6","199.5","203.5","206.5",
                  "210.7","218.1","225.7","229.1","233.6","241.8","250.3","254.1"});

    if (rigCaps.commands.contains(funcVFOEqualAB)) {
        VFO = QStringList({"VFOA","VFOB"});
    } else if (rigCaps.commands.contains(funcVFOEqualMS)) {
        VFO = QStringList({"Main","Sub"});
    }

    dsql = QStringList({"OFF","DSQL","CSQL"});
    dtcsp = QStringList({"NN","NR","RN","RR"});

    foreach (auto mode, rigCaps.modes){
        modes.append(mode.name);
    }

    numEditor = new tableEditor(QRegularExpression("[0-9]{0,3}"),ui->table);
    ui->table->setItemDelegateForColumn(columnNum, numEditor);

    if (rigCaps.memGroups>1)
        nameEditor = new tableEditor(QRegularExpression("[0-9A-Za-z\\/\\ ]{0,16}$"),ui->table);
    else
        nameEditor = new tableEditor(QRegularExpression("[0-9A-Za-z\\/\\ ]{0,10}$"),ui->table);

    ui->table->setItemDelegateForColumn(columnName, nameEditor);

    memoryList = new tableCombobox(createModel(memoryModel, memory),false,ui->table);
    ui->table->setItemDelegateForColumn(columnMemory, memoryList);

    vfoList = new tableCombobox(createModel(vfoModel, VFO),false,ui->table);
    ui->table->setItemDelegateForColumn(columnVFO, vfoList);

    freqEditor = new tableEditor(QRegularExpression("[0-9]{0,10}"),ui->table);
    ui->table->setItemDelegateForColumn(columnFrequency, freqEditor);


    modesList = new tableCombobox(createModel(modesModel, modes),false,ui->table);
    ui->table->setItemDelegateForColumn(columnMode, modesList);

    dataList = new tableCombobox(createModel(dataModel, dataModes),false,ui->table);
    ui->table->setItemDelegateForColumn(columnData, dataList);

    filterList = new tableCombobox(createModel(filterModel, filters),false,ui->table);
    ui->table->setItemDelegateForColumn(columnFilter, filterList);

    duplexList = new tableCombobox(createModel(duplexModel, duplexModes),false,ui->table);
    ui->table->setItemDelegateForColumn(columnDuplex, duplexList);

    toneModesList = new tableCombobox(createModel(toneModesModel, toneModes),false,ui->table);
    ui->table->setItemDelegateForColumn(columnToneMode, toneModesList);

    tonesList = new tableCombobox(createModel(tonesModel, tones),false,ui->table);
    ui->table->setItemDelegateForColumn(columnTone, tonesList);

    tsqlList = new tableCombobox(createModel(tsqlModel, tones),false,ui->table);
    ui->table->setItemDelegateForColumn(columnTSQL, tsqlList);

    dsqlList = new tableCombobox(createModel(dsqlModel, dsql),false,ui->table);
    ui->table->setItemDelegateForColumn(columnDSQL, dsqlList);

    dtcsEditor = new tableEditor(QRegularExpression("[0-9]{0,3}"),ui->table);
    ui->table->setItemDelegateForColumn(columnDTCS, dtcsEditor);

    dtcspList = new tableCombobox(createModel(dtcspModel, dtcsp),false,ui->table);
    ui->table->setItemDelegateForColumn(columnDTCSPolarity, dtcspList);

    offsetEditor = new tableEditor(QRegularExpression("[0-9]{0,7}"),ui->table);
    ui->table->setItemDelegateForColumn(columnOffset, offsetEditor);

    dvsqlEditor = new tableEditor(QRegularExpression("[0-9]{0,2}"),ui->table);
    ui->table->setItemDelegateForColumn(columnDVSquelch, dvsqlEditor);

    urEditor = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
    ui->table->setItemDelegateForColumn(columnUR, urEditor);

    r1Editor = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
    ui->table->setItemDelegateForColumn(columnR1, r1Editor);

    r2Editor = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
    ui->table->setItemDelegateForColumn(columnR2, r2Editor);

    connect(ui->table,SIGNAL(rowAdded(int)),this,SLOT(rowAdded(int)));
    connect(ui->table,SIGNAL(rowDeleted(quint32)),this,SLOT(rowDeleted(quint32)));

    ui->table->sortByColumn(0,Qt::AscendingOrder);

}

void memories::populate()
{
    ui->group->setCurrentIndex(0);
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
    quint32 prev=0;
    ui->table->blockSignals(true);
    for (int i=0;i < ui->table->rowCount();i++)
    {
        if (ui->table->item(i,columnNum) == NULL)
            continue;
        quint32 num = ui->table->item(i,columnNum)->text().toInt();
        if (num>prev+1)
        {
            // We have a gap;
            QPushButton* recall = new QPushButton("Recall");
            ui->table->setCellWidget(row,columnRecall,recall);
            connect(recall, &QPushButton::clicked, this,
                    [=]() { qInfo() << "Recalling" << prev+1; emit recallMemory(quint32((ui->group->currentIndex()+1) << 16) | (prev+1));});
            ui->table->model()->setData(ui->table->model()->index(row,columnNum),QString::number(prev+1).rightJustified(3,'0'));
            // Set default values (where possible) for all other values:
            if (ui->table->item(row,columnMemory) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnMemory),memory[0]);
            if (ui->table->item(row,columnVFO) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnVFO),VFO[0]);
            if (ui->table->item(row,columnData) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnData),dataModes[0]);
            if (ui->table->item(row,columnDuplex) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDuplex),duplexModes[0]);
            if (ui->table->item(row,columnToneMode) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnToneMode),toneModes[0]);
            if (ui->table->item(row,columnDuplex) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDSQL),dsql[0]);
            if (ui->table->item(row,columnTone) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnTone),"67.0");
            if (ui->table->item(row,columnTSQL) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnTSQL),"67.0");
            if (ui->table->item(row,columnDTCS) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCS),"023");
            if (ui->table->item(row,columnDTCSPolarity) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCSPolarity),dtcsp[0]);
            if (ui->table->item(row,columnDVSquelch) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDVSquelch),"00");
            if (ui->table->item(row,columnOffset) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnOffset),"0");
            if (ui->table->item(row,columnUR) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnUR),"        ");
            if (ui->table->item(row,columnR1) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnR1),"        ");
            if (ui->table->item(row,columnR2) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnR2),"        ");
            if (ui->table->item(row,columnName) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnName),"          ");

            ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() | Qt::ItemIsEditable);
            break;
        }
        prev=num;
    }
    ui->table->blockSignals(false);
}

void memories::rowDeleted(quint32 mem)

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

    currentMemory->group = ui->group->currentIndex()+1;
    currentMemory->channel = (ui->table->item(row,columnNum) == NULL) ? 0 : ui->table->item(row,columnNum)->text().toInt();

    for (quint8 f=0;f<memory.size();f++)
    {
        if ((ui->table->item(row,columnMemory) == NULL) ? false : ui->table->item(row,columnMemory)->text()==memory[f]) {
            currentMemory->memory=f;
            break;
        }
    }

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
            currentMemory->filter=f+1; // Filters start at 1
            break;
        }
    }

    for (quint8 f=0;f<duplexModes.size();f++)
    {
        if ((ui->table->item(row,columnDuplex) == NULL) ? false : ui->table->item(row,columnDuplex)->text()==duplexModes[f]) {
            currentMemory->duplex=f;
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

    for (quint8 f=0;f<dsql.size();f++)
    {
        if ((ui->table->item(row,columnDSQL) == NULL) ? false : ui->table->item(row,columnDSQL)->text()==dsql[f]) {
            currentMemory->dsql=f;
            break;
        }
    }

    currentMemory->tone = (ui->table->item(row,columnTone) == NULL) ? 670 : int(ui->table->item(row,columnTone)->text().toFloat()*10.0);

    currentMemory->tsql = (ui->table->item(row,columnTSQL) == NULL) ? 670 : int(ui->table->item(row,columnTSQL)->text().toFloat()*10.0);

    currentMemory->dtcs = (ui->table->item(row,columnDTCS) == NULL) ? 23 : int(ui->table->item(row,columnDTCS)->text().toUInt());

    for (quint8 f=0;f<dtcsp.size();f++)
    {
        if ((ui->table->item(row,columnDTCSPolarity) == NULL) ? false : ui->table->item(row,columnDTCSPolarity)->text()==dtcsp[f]) {
            currentMemory->dtcsp=f;
            break;
        }
    }

    currentMemory->dvsql = (ui->table->item(row,columnDVSquelch) == NULL) ? 0 : int(ui->table->item(row,columnDVSquelch)->text().toUInt());
    currentMemory->duplexOffset.Hz = (ui->table->item(row,columnOffset) == NULL) ? 0 : int(ui->table->item(row,columnOffset)->text().toULongLong());
    currentMemory->duplexOffset.MHzDouble=currentMemory->duplexOffset.Hz/1000000.0;
    currentMemory->duplexOffset.VFO=selVFO_t::activeVFO;

    memcpy(currentMemory->UR,((ui->table->item(row,columnUR) == NULL) ? "" : ui->table->item(row,columnUR)->text()).toStdString().c_str(),8);
    memcpy(currentMemory->R1,((ui->table->item(row,columnR1) == NULL) ? "" : ui->table->item(row,columnR1)->text()).toStdString().c_str(),8);
    memcpy(currentMemory->R2,((ui->table->item(row,columnR2) == NULL) ? "" : ui->table->item(row,columnR2)->text()).toStdString().c_str(),8);

    memcpy(currentMemory->name,((ui->table->item(row,columnName) == NULL) ? "" : ui->table->item(row,columnName)->text()).toStdString().c_str(),16);

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

void memories::on_group_currentIndexChanged(int index)
{
    ui->group->setEnabled(false);
    ui->table->blockSignals(true);
    ui->table->setRowCount(0);
    ui->table->blockSignals(false);
    for (quint16 m=1;m<=rigCaps.memories;m++)
    {
        if (rigCaps.memGroups>1)
            emit getMemory(quint32((index+1)<<16) | (m & 0xffff));
        else
            emit getMemory(quint32(m & 0xffff));
    }
}

void memories::on_vfoMode_clicked()
{
    emit vfoMode();
}

void memories::on_memoryMode_clicked()
{
    emit memoryMode();
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
                [=]() { qInfo() << "Recalling" << mem.channel; emit recallMemory(((mem.group << 16) | mem.channel));});

        ui->table->model()->setData(ui->table->model()->index(row,columnNum),QString::number(mem.channel & 0xffff).rightJustified(3,'0'));
        ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() & (~Qt::ItemIsEditable));

        for (int f=0;f<memory.size();f++)
        {
            if (f==mem.memory) {
                ui->table->model()->setData(ui->table->model()->index(row,columnMemory),memory[f]);
                break;
            }
        }

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

        for (int f=0;f<duplexModes.size();f++)
        {
            if (f==mem.duplex) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDuplex),duplexModes[f]);
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

        for (int f=0;f<dsql.size();f++)
        {
            if (f==mem.dsql) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDSQL),dsql[f]);
                break;
            }
        }

        ui->table->model()->setData(ui->table->model()->index(row,columnTone),QString::number((float)mem.tone/10,'f',1));

        ui->table->model()->setData(ui->table->model()->index(row,columnTSQL),QString::number((float)mem.tsql/10,'f',1));

        ui->table->model()->setData(ui->table->model()->index(row,columnDTCS),QString::number(mem.dtcs).rightJustified(3,'0'));

        for (int f=0;f<dtcsp.size();f++)
        {
            if (f==mem.dtcsp) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDTCSPolarity),dtcsp[f]);
                break;
            }
        }

        ui->table->model()->setData(ui->table->model()->index(row,columnDVSquelch),QString::number(mem.dvsql).rightJustified(2,'0'));

        ui->table->model()->setData(ui->table->model()->index(row,columnOffset),QString::number(mem.duplexOffset.Hz));

        ui->table->model()->setData(ui->table->model()->index(row,columnUR),QString(mem.UR));

        ui->table->model()->setData(ui->table->model()->index(row,columnR1),QString(mem.R1));

        ui->table->model()->setData(ui->table->model()->index(row,columnR2),QString(mem.R2));

        ui->table->model()->setData(ui->table->model()->index(row,columnName),QString(mem.name));

        ui->table->blockSignals(false);
    }

    // If this is the last channel, re-enable the group combo.
    if (mem.channel == rigCaps.memories) {
        ui->group->setEnabled(true);
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
