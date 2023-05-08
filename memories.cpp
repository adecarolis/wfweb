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

        columnRecall=0, columnNum,columnSplit,columnScan,columnFrequency,columnMode,columnFilter,columnData,columnDuplex,columnToneMode,columnDSQL,columnTone,columnTSQL,columnDTCS,
        columnDTCSPolarity,columnDVSquelch,columnOffset,columnUR,columnR1,columnR2,columnFrequencyB,columnModeB,columnFilterB,columnDataB,columnDuplexB,columnToneModeB, columnDSQLB
        columnToneB,columnTSQLB,columnDTCSB,columnDTCSPolarityB,columnDVSquelchB,columnOffsetB,columnURB,columnR1B,columnR2B,columnName,
     */

    headers << "" << "Num" << "Name" << "Split" << "Scan" << "VFO" << "Freq" << "Mode" << "Filter" << "Data" <<"Duplex" << "Tn Mode" << "DSQL" << "Tone" << "TSQL" <<
        "DTCS" << "DTCS Pol" << "DV Sql" << "Offset" << "UR" << "R1" << "R2" << "VFO B" << "Freq B" << "Mode B" << "Filter B" << "Data B" << "Duplex B" <<
        "Tn Mode B" << "DSQL B" << "Tone B" << "TSQL B" << "DTCS B" << "DTCSP B" << "DV Sql B" << "Offset B" << "UR B" << "R1 B" << "R2 B";

    ui->table->setHorizontalHeaderLabels(headers);

    ui->groupLabel->hide();
    ui->group->hide();
    ui->vfoMode->hide();
    ui->memoryMode->hide();
    ui->loadingMemories->setVisible(false);
    ui->loadingMemories->setStyleSheet("QLabel {color: #ff0000}");

    ui->group->blockSignals(true);
    for (int i=rigCaps.memStart;i<=rigCaps.memGroups;i++) {

        ui->group->addItem(QString("Group %0").arg(i,2,10,QChar('0')),i);
    }
    if (rigCaps.satMemories && rigCaps.commands.contains(funcSatelliteMode)){
        ui->group->addItem("Satellite",200);
    }
    ui->group->setCurrentIndex(-1);
    ui->group->blockSignals(false);


    scan = QStringList({"OFF","*1","*2","*3"});
    split = QStringList({"OFF","ON"});

    dataModes = QStringList({"OFF","DATA1"});
    if (rigCaps.commands.contains(funcDATA2Mod))
        dataModes.append("DATA2");
    if (rigCaps.commands.contains(funcDATA3Mod))
        dataModes.append("DATA3");

    filters = QStringList({"FIL1","FIL2","FIL3"});

    duplexModes = QStringList({"OFF","DUP-","DUP+","RPS"});

    toneModes = QStringList({"OFF","TONE","TSQL"});
    if (rigCaps.commands.contains(funcRepeaterDTCS))
        toneModes.append("DTCS");

    tones = QStringList({"67.0","69.3","71.9","74.4","77.0","79.7","82.5","85.4","88.5","91.5","94.8","97.4","100.0","103.5","107.2","110.9","114.8","118.8","123.0","127.3","131.8","136.5",
                  "141.3","146.2","151.4","156.7","159.8","162.2","165.5","167.9","171.3","173.8","177.3","179.9","183.5","186.2","189.9","192.8","196.6","199.5","203.5","206.5",
                  "210.7","218.1","225.7","229.1","233.6","241.8","250.3","254.1"});

    dtcs = QStringList({"023","025","026","031","032","036","043","047","051","053","054","065","071","072","073","074","114","115","116","122","125","131","132","134","143","145","152","155","156","162","165",
                        "172","174","205","212","223","225","226","243","244","245","246","251","252","255","261","263","265","266","271","274","306","311","315","325","331","332","343","346",
                        "351","356","364","365","371","411","412","413","423","431","432","445","446","452","454","455","462","464","465","466","503","506","516","523","526","532","546","565",
                        "606","612","624","627","631","632","654","662","664","703","712","723","731","732","734","743","754"});

    if (rigCaps.commands.contains(funcVFOEqualAB)) {
        vfos = QStringList({"VFOA","VFOB"});
    } else if (rigCaps.commands.contains(funcVFOEqualMS)) {
        vfos = QStringList({"MAIN","SUB"});
    }

    foreach (auto mode, rigCaps.modes){
        modes.append(mode.name);
    }

    dsql = QStringList({"OFF","DSQL","CSQL"});

    dtcsp = QStringList({"BOTH N","N/R","R/N","BOTH R"});

    connect(ui->table,SIGNAL(rowAdded(int)),this,SLOT(rowAdded(int)));
    connect(ui->table,SIGNAL(rowDeleted(quint32)),this,SLOT(rowDeleted(quint32)));
    connect(&timeoutTimer, SIGNAL(timeout()), this, SLOT(timeout()));

    ui->table->sortByColumn(columnRecall,Qt::AscendingOrder);

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

    quint32 num=rigCaps.memStart;

    /* This feels unnecessarily complicated, but here we are:
        * Set the memory number to 1
        * create a vector of all current memory numbers (ignoring any that are NULL)
        * If empty use 1 as the memory number
        * if not, sort it into numerical order
        * check if there is a gap using adjacent_find iterator (use that if there is)
        * If not, go back to previous, check we haven't exceeded the total allowed and if not add 1 to previous
        * If no empty memories available, log a warning and quit
    */

    std::vector<quint16> rows;
    ui->table->blockSignals(true);
    for (int i=0;i < ui->table->rowCount();i++)
    {
        if (ui->table->item(i,columnNum) != NULL)
        {
            rows.push_back(ui->table->item(i,columnNum)->text().toUInt());
        }
    }

    if (rows.size()!=0)
    {
        std::sort(rows.begin(),rows.end());
        auto i = std::adjacent_find( rows.begin(), rows.end(), [](int l, int r){return l+1<r;} );
        if (i == rows.end())
        {
            // No gaps found so work on highest value found
            if ((ui->group->currentData().toInt() != 200 && rows.back() < groupMemories-1) || (ui->group->currentData().toInt() == 200 && rows.back() < rigCaps.satMemories-1))
            {
                num = rows.back() + 1;
            }
            else if (rows.front() == rigCaps.memStart)
            {
                qWarning() << "Sorry no free memory slots found, please delete one first";
                ui->table->removeRow(row);
            }
        } else {
            num = 1 + *i;
        }
    }

    QPushButton* recall = new QPushButton("Recall");
    ui->table->setCellWidget(row,columnRecall,recall);
    connect(recall, &QPushButton::clicked, this,
            [=]() { qInfo() << "Recalling" << num; emit recallMemory((quint32((ui->group->currentData().toUInt() << 16) | num)));});
    ui->table->model()->setData(ui->table->model()->index(row,columnNum),QString::number(num).rightJustified(3,'0'));
    // Set default values (where possible) for all other values:
    if (ui->table->item(row,columnSplit) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnSplit),split[0]);
    if (ui->table->item(row,columnScan) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnScan),scan[0]);
    if (ui->table->item(row,columnData) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnData),dataModes[0]);
    if (ui->table->item(row,columnFilter) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnFilter),filters[0]);
    if (ui->table->item(row,columnDuplex) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDuplex),duplexModes[0]);
    if (ui->table->item(row,columnToneMode) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnToneMode),toneModes[0]);
    if (ui->table->item(row,columnDSQL) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDSQL),dsql[0]);
    if (ui->table->item(row,columnTone) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnTone),tones[0]);
    if (ui->table->item(row,columnTSQL) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnTSQL),tones[0]);
    if (ui->table->item(row,columnDTCSPolarity) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCSPolarity),dtcsp[0]);
    if (ui->table->item(row,columnDTCS) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCS),dtcs[0]);
    if (ui->table->item(row,columnDVSquelch) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDVSquelch),"00");
    if (ui->table->item(row,columnOffset) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnOffset),"0.000");
    if (ui->table->item(row,columnUR) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnUR),"CQCQCQ  ");
    if (ui->table->item(row,columnR1) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnR1),"        ");
    if (ui->table->item(row,columnR2) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnR2),"        ");
    if (ui->table->item(row,columnDataB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDataB),dataModes[0]);
    if (ui->table->item(row,columnFilterB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnFilterB),filters[0]);
    if (ui->table->item(row,columnToneModeB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnToneModeB),toneModes[0]);
    if (ui->table->item(row,columnDSQLB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDSQLB),dsql[0]);
    if (ui->table->item(row,columnToneB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnToneB),tones[0]);
    if (ui->table->item(row,columnTSQLB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnTSQLB),tones[0]);
    if (ui->table->item(row,columnDTCSPolarityB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCSPolarityB),dtcsp[0]);
    if (ui->table->item(row,columnDTCSB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCSB),dtcs[0]);
    if (ui->table->item(row,columnDVSquelchB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDVSquelchB),"00");
    if (ui->table->item(row,columnOffsetB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnOffsetB),"0.000");
    if (ui->table->item(row,columnDuplexB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDuplexB),duplexModes[0]);
    if (ui->table->item(row,columnURB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnURB),"CQCQCQ  ");
    if (ui->table->item(row,columnR1B) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnR1B),"        ");
    if (ui->table->item(row,columnR2B) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnR2B),"        ");
    ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() | Qt::ItemIsEditable);
    ui->table->blockSignals(false);

    on_table_cellChanged(row,columnNum); // Force an attempt to store this row.
}

void memories::rowDeleted(quint32 mem)

{
    if (mem && mem <= rigCaps.memories) {
        qInfo() << "Mem Deleted" << mem;
        emit clearMemory((quint32((ui->group->currentData().toInt() << 16) | mem)));
    }
}

void memories::on_table_cellChanged(int row, int col)
{
    // If the import is updating a hidden column, ignore it.
    if (ui->table->isColumnHidden(col))
        return;

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

    currentMemory->group = ui->group->currentData().toInt();
    currentMemory->channel = (ui->table->item(row,columnNum) == NULL) ? 0 : ui->table->item(row,columnNum)->text().toInt();

    if (currentMemory->group==200) {
        currentMemory->sat=true;
    }

    ui->table->blockSignals(true);

    switch (col)
    {
    case columnVFO:
        if (ui->table->item(row,columnVFOB) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnVFOB),ui->table->item(row,columnVFO)->text());
        break;
    case columnVFOB:
        if (ui->table->item(row,columnVFO) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnVFO),ui->table->item(row,columnVFOB)->text());
        break;
    case columnFrequency:
        if (ui->table->item(row,columnFrequencyB) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnFrequencyB),ui->table->item(row,columnFrequency)->text());
        break;
    case columnFrequencyB:
        if (ui->table->item(row,columnFrequency) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnFrequency),ui->table->item(row,columnFrequencyB)->text());
        break;
    case columnMode:
        if (ui->table->item(row,columnModeB) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnModeB),ui->table->item(row,columnMode)->text());
        break;
    case columnModeB:
        if (ui->table->item(row,columnMode) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnMode),ui->table->item(row,columnModeB)->text());
        break;
    case columnDuplex:
        ui->table->model()->setData(ui->table->model()->index(row,columnDuplexB),ui->table->item(row,columnDuplex)->text());
        break;
    case columnDuplexB:
        ui->table->model()->setData(ui->table->model()->index(row,columnDuplex),ui->table->item(row,columnDuplexB)->text());
        break;
//    case columnOffset:
//        ui->table->model()->setData(ui->table->model()->index(row,columnOffsetB),ui->table->item(row,columnOffset)->text());
//        break;
//    case columnOffsetB:
//        ui->table->model()->setData(ui->table->model()->index(row,columnOffset),ui->table->item(row,columnOffsetB)->text());
//        break;
    default:
        break;
    }

    ui->table->blockSignals(false);

    // The table shouldn't be updated below

    for (quint8 f=0;f<split.size();f++)
    {
        if ((ui->table->item(row,columnSplit) == NULL) ? false : ui->table->item(row,columnSplit)->text().toUpper()==split[f].toUpper()) {
            currentMemory->split=f;
            break;
        }
    }

    for (quint8 f=0;f<scan.size();f++)
    {
        if ((ui->table->item(row,columnScan) == NULL) ? false : ui->table->item(row,columnScan)->text().toUpper()==scan[f].toUpper()) {
            currentMemory->scan=f;
            break;
        }
    }

    for (quint8 f=0;f<vfos.size();f++)
    {
        if ((ui->table->item(row,columnVFO) == NULL) ? false : ui->table->item(row,columnVFO)->text().toUpper()==vfos[f].toUpper()) {
            currentMemory->vfo=f;
            break;
        }
        if ((ui->table->item(row,columnVFOB) == NULL) ? false : ui->table->item(row,columnVFOB)->text().toUpper()==vfos[f].toUpper()) {
            currentMemory->vfoB=f;
            break;
        }
    }

    currentMemory->frequency.Hz = (ui->table->item(row,columnFrequency) == NULL) ? 0 : quint64(ui->table->item(row,columnFrequency)->text().toDouble()*1000000.0);
    currentMemory->frequencyB.Hz = (ui->table->item(row,columnFrequencyB) == NULL) ? 0 : quint64(ui->table->item(row,columnFrequencyB)->text().toDouble()*1000000.0);

    foreach (auto m, rigCaps.modes){
        if ((ui->table->item(row,columnMode) == NULL) ? false : ui->table->item(row,columnMode)->text()==m.name) {
            currentMemory->mode=m.reg;
        }
        if ((ui->table->item(row,columnModeB) == NULL) ? false : ui->table->item(row,columnModeB)->text()==m.name) {
            currentMemory->modeB=m.reg;
        }
    }

    for (quint8 f=0;f<dataModes.size();f++)
    {
        if ((ui->table->item(row,columnData) == NULL) ? false : ui->table->item(row,columnData)->text()==dataModes[f]) {
            currentMemory->datamode=f;
        }
        if ((ui->table->item(row,columnDataB) == NULL) ? false : ui->table->item(row,columnDataB)->text()==dataModes[f]) {
            currentMemory->datamodeB=f;
        }
    }

    for (quint8 f=0;f<filters.size();f++)
    {
        if ((ui->table->item(row,columnFilter) == NULL) ? false : ui->table->item(row,columnFilter)->text()==filters[f]) {
            currentMemory->filter=f+1; // Filters start at 1
        }
        if ((ui->table->item(row,columnFilterB) == NULL) ? false : ui->table->item(row,columnFilterB)->text()==filters[f]) {
            currentMemory->filterB=f+1; // Filters start at 1
        }
    }

    for (quint8 f=0;f<duplexModes.size();f++)
    {
        if ((ui->table->item(row,columnDuplex) == NULL) ? false : ui->table->item(row,columnDuplex)->text()==duplexModes[f]) {
            currentMemory->duplex=f;
        }
        if ((ui->table->item(row,columnDuplexB) == NULL) ? false : ui->table->item(row,columnDuplexB)->text()==duplexModes[f]) {
            currentMemory->duplexB=f;
        }

    }

    for (quint8 f=0;f<toneModes.size();f++)
    {
        if ((ui->table->item(row,columnToneMode) == NULL) ? false : ui->table->item(row,columnToneMode)->text().toUpper()==toneModes[f].toUpper()) {
            currentMemory->tonemode=f;
        }
        if ((ui->table->item(row,columnToneModeB) == NULL) ? false : ui->table->item(row,columnToneModeB)->text().toUpper()==toneModes[f].toUpper()) {
            currentMemory->tonemodeB=f;
        }
    }

    for (quint8 f=0;f<dsql.size();f++)
    {
        if ((ui->table->item(row,columnDSQL) == NULL) ? false : ui->table->item(row,columnDSQL)->text()==dsql[f]) {
            currentMemory->dsql=f;
        }
        if ((ui->table->item(row,columnDSQLB) == NULL) ? false : ui->table->item(row,columnDSQLB)->text()==dsql[f]) {
            currentMemory->dsqlB=f;
        }
    }

    currentMemory->tone = (ui->table->item(row,columnTone) == NULL) ? 670 : int(ui->table->item(row,columnTone)->text().toFloat()*10.0);
    currentMemory->toneB = (ui->table->item(row,columnToneB) == NULL) ? 670 : int(ui->table->item(row,columnToneB)->text().toFloat()*10.0);

    currentMemory->tsql = (ui->table->item(row,columnTSQL) == NULL) ? 670 : int(ui->table->item(row,columnTSQL)->text().toFloat()*10.0);
    currentMemory->tsqlB = (ui->table->item(row,columnTSQLB) == NULL) ? 670 : int(ui->table->item(row,columnTSQLB)->text().toFloat()*10.0);

    currentMemory->dtcs = (ui->table->item(row,columnDTCS) == NULL) ? 23 : int(ui->table->item(row,columnDTCS)->text().toUInt());
    currentMemory->dtcsB = (ui->table->item(row,columnDTCSB) == NULL) ? 23 : int(ui->table->item(row,columnDTCSB)->text().toUInt());

    for (quint8 f=0;f<dtcsp.size();f++)
    {
        if ((ui->table->item(row,columnDTCSPolarity) == NULL) ? false : ui->table->item(row,columnDTCSPolarity)->text().toUpper()==dtcsp[f].toUpper()) {
            currentMemory->dtcsp=f;
        }
        if ((ui->table->item(row,columnDTCSPolarityB) == NULL) ? false : ui->table->item(row,columnDTCSPolarityB)->text().toUpper()==dtcsp[f].toUpper()) {
            currentMemory->dtcspB=f;
        }
    }

    currentMemory->dvsql = (ui->table->item(row,columnDVSquelch) == NULL) ? 0 : int(ui->table->item(row,columnDVSquelch)->text().toUInt());
    currentMemory->dvsqlB = (ui->table->item(row,columnDVSquelchB) == NULL) ? 0 : int(ui->table->item(row,columnDVSquelchB)->text().toUInt());

    currentMemory->duplexOffset.MHzDouble = (ui->table->item(row,columnOffset) == NULL) ? 0.0 : ui->table->item(row,columnOffset)->text().toDouble();
    currentMemory->duplexOffset.Hz=currentMemory->duplexOffset.MHzDouble*1000000.0;
    currentMemory->duplexOffset.VFO=selVFO_t::activeVFO;

    currentMemory->duplexOffsetB.MHzDouble = (ui->table->item(row,columnOffsetB) == NULL) ? 0.0 : ui->table->item(row,columnOffsetB)->text().toDouble();
    currentMemory->duplexOffsetB.Hz=currentMemory->duplexOffsetB.MHzDouble*1000000.0;
    currentMemory->duplexOffsetB.VFO=selVFO_t::activeVFO;


    memcpy(currentMemory->UR,((ui->table->item(row,columnUR) == NULL) ? "" : ui->table->item(row,columnUR)->text()).toStdString().c_str(),8);
    memcpy(currentMemory->URB,((ui->table->item(row,columnURB) == NULL) ? "" : ui->table->item(row,columnURB)->text()).toStdString().c_str(),8);

    memcpy(currentMemory->R1,((ui->table->item(row,columnR1) == NULL) ? "" : ui->table->item(row,columnR1)->text()).toStdString().c_str(),8);
    memcpy(currentMemory->R1B,((ui->table->item(row,columnR1B) == NULL) ? "" : ui->table->item(row,columnR1B)->text()).toStdString().c_str(),8);

    memcpy(currentMemory->R2,((ui->table->item(row,columnR2) == NULL) ? "" : ui->table->item(row,columnR2)->text()).toStdString().c_str(),8);
    memcpy(currentMemory->R2B,((ui->table->item(row,columnR2B) == NULL) ? "" : ui->table->item(row,columnR2B)->text()).toStdString().c_str(),8);

    memcpy(currentMemory->name,((ui->table->item(row,columnName) == NULL) ? "" : ui->table->item(row,columnName)->text()).toStdString().c_str(),16);

    // Only write the memory if ALL values are non-null
    bool write=true;
    for (int f=1; f<ui->table->columnCount();f++)
    {
        if (!ui->table->isColumnHidden(f) && ui->table->item(row,f) == NULL)
            write=false;
    }
    if (write) {
        emit setMemory(*currentMemory);
        qInfo() << "Sending memory, group:" << currentMemory->group << "channel" << currentMemory->channel;
        // Set number to not be editable once written. Not sure why but this crashes?
        //ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() & (~Qt::ItemIsEditable));
    }
}

void memories::on_group_currentIndexChanged(int index)
{
    Q_UNUSED(index)

    timeoutTimer.start(MEMORY_TIMEOUT);


    // Special case for group 100 on the IC705!
    if (ui->group->currentData().toInt() == 200)
        groupMemories=rigCaps.satMemories;
    else if (ui->group->currentData().toInt() == 100)
        groupMemories=3;
    else
        groupMemories=rigCaps.memories;

    ui->loadingMemories->setVisible(true);
    ui->table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ui->group->setEnabled(false);
    ui->table->blockSignals(true);
    ui->table->setRowCount(0);
    ui->table->blockSignals(false);

    // Hide all columns except recall
    for (int i=1;i<ui->table->columnCount();i++)
    {
        ui->table->hideColumn(i);
    }

    QVector<memParserFormat> parser;

    if (ui->group->currentData().toInt() == 200) {
        emit setSatelliteMode(true);
        parser = rigCaps.satParser;
    } else {
        emit setSatelliteMode(false);
        parser = rigCaps.memParser;
    }

    int visibleRows=1;

    foreach (auto parse, parser) {
        switch (parse.spec)
        {
        case 'a':
            ui->groupLabel->show();
            ui->group->show();
            ui->vfoMode->show();
            ui->memoryMode->show();
            break;
        case 'b':
            if (numEditor != Q_NULLPTR)
                delete numEditor;
            numEditor = new tableEditor(QRegularExpression("[0-9]{0,3}"),ui->table);
            ui->table->setItemDelegateForColumn(columnNum, numEditor);
            ui->table->showColumn(columnNum);
            visibleRows++;
            break;
        case 'c':
            if (scanList != Q_NULLPTR)
                delete scanList;
            scanList = new tableCombobox(createModel(scanModel, scan),false,ui->table);
            ui->table->setItemDelegateForColumn(columnScan, scanList);

            ui->table->showColumn(columnScan);
            visibleRows++;
            break;
        case 'd':
            if (splitList != Q_NULLPTR)
                delete splitList;
            splitList = new tableCombobox(createModel(splitModel, split),false,ui->table);
            ui->table->setItemDelegateForColumn(columnSplit, splitList);

            if (scanList != Q_NULLPTR)
                delete scanList;
            scanList = new tableCombobox(createModel(scanModel, scan),false,ui->table);
            ui->table->setItemDelegateForColumn(columnScan, scanList);

            ui->table->showColumn(columnSplit);
            ui->table->showColumn(columnScan);

            visibleRows++;
            visibleRows++;
            break;

        case 'e':
            if (vfoList != Q_NULLPTR)
                delete vfoList;
            vfoList = new tableCombobox(createModel(vfoModel, vfos),false,ui->table);
            ui->table->setItemDelegateForColumn(columnVFO, vfoList);

            ui->table->showColumn(columnVFO);
            visibleRows++;
            break;
        case 'E':
            if (vfoListB != Q_NULLPTR)
                delete vfoListB;
            vfoListB = new tableCombobox(createModel(vfoModelB, vfos),false,ui->table);
            ui->table->setItemDelegateForColumn(columnVFOB, vfoListB);

            ui->table->showColumn(columnVFOB);
            visibleRows++;
            break;
        case 'f':
            if (freqEditor != Q_NULLPTR)
                delete freqEditor;
            freqEditor = new tableEditor(QRegularExpression("[0-9.]{0,10}"),ui->table);
            ui->table->setItemDelegateForColumn(columnFrequency, freqEditor);

            ui->table->showColumn(columnFrequency);
            visibleRows++;
            break;
        case 'F':
            if (freqEditorB != Q_NULLPTR)
                delete freqEditorB;
            freqEditorB = new tableEditor(QRegularExpression("[0-9.]{0,10}"),ui->table);
            ui->table->setItemDelegateForColumn(columnFrequencyB, freqEditorB);

            ui->table->showColumn(columnFrequencyB);
            visibleRows++;
            break;
        case 'g':
            if (modesList != Q_NULLPTR)
                delete modesList;
            modesList = new tableCombobox(createModel(modesModel, modes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnMode, modesList);

            ui->table->showColumn(columnMode);
            visibleRows++;
            break;
        case 'G':
            if (modesListB != Q_NULLPTR)
                delete modesListB;
            modesListB = new tableCombobox(createModel(modesModelB, modes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnModeB, modesListB);

            ui->table->showColumn(columnModeB);
            visibleRows++;
            break;
        case 'h':
            if (filterList != Q_NULLPTR)
                delete filterList;
            filterList = new tableCombobox(createModel(filterModel, filters),false,ui->table);
            ui->table->setItemDelegateForColumn(columnFilter, filterList);

            ui->table->showColumn(columnFilter);
            visibleRows++;
            break;
        case 'H':
            if (filterListB != Q_NULLPTR)
                delete filterListB;
            filterListB = new tableCombobox(createModel(filterModelB, filters),false,ui->table);
            ui->table->setItemDelegateForColumn(columnFilterB, filterListB);

            ui->table->showColumn(columnFilterB);
            visibleRows++;
            break;
        case 'i':
            if (dataList != Q_NULLPTR)
                delete dataList;
            dataList = new tableCombobox(createModel(dataModel, dataModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnData, dataList);

            ui->table->showColumn(columnData);
            visibleRows++;
            break;
        case 'I':
            if (dataListB != Q_NULLPTR)
                delete dataListB;
            dataListB = new tableCombobox(createModel(dataModelB, dataModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDataB, dataListB);

            ui->table->showColumn(columnDataB);
            visibleRows++;
            break;
        case 'j':
            if (duplexList != Q_NULLPTR)
                delete duplexList;
            duplexList = new tableCombobox(createModel(duplexModel, duplexModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDuplex, duplexList);

            if (toneModesList != Q_NULLPTR)
                delete toneModesList;
            toneModesList = new tableCombobox(createModel(toneModesModel, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneMode, toneModesList);

            ui->table->showColumn(columnDuplex);
            ui->table->showColumn(columnToneMode);
            visibleRows++;
            visibleRows++;
            break;
        case 'J':
            if (duplexListB != Q_NULLPTR)
                delete duplexListB;
            duplexListB = new tableCombobox(createModel(duplexModelB, duplexModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDuplexB, duplexListB);

            if (toneModesListB != Q_NULLPTR)
                delete toneModesListB;
            toneModesListB = new tableCombobox(createModel(toneModesModelB, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneModeB, toneModesListB);

            ui->table->showColumn(columnDuplexB);
            ui->table->showColumn(columnToneModeB);
            visibleRows++;
            visibleRows++;
            break;
        case 'k':
            if (dataList != Q_NULLPTR)
                delete dataList;
            dataList = new tableCombobox(createModel(dataModel, dataModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnData, dataList);

            if (toneModesList != Q_NULLPTR)
                delete toneModesList;
            toneModesList = new tableCombobox(createModel(toneModesModel, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneMode, toneModesList);

            ui->table->showColumn(columnData);
            ui->table->showColumn(columnToneMode);
            visibleRows++;
            visibleRows++;
            break;
        case 'K':
            if (dataListB != Q_NULLPTR)
                delete dataListB;
            dataListB = new tableCombobox(createModel(dataModelB, dataModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDataB, dataListB);

            if (toneModesListB != Q_NULLPTR)
                delete toneModesListB;
            toneModesListB = new tableCombobox(createModel(toneModesModelB, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneModeB, toneModesListB);

            ui->table->showColumn(columnDataB);
            ui->table->showColumn(columnToneModeB);
            visibleRows++;
            visibleRows++;
            break;
        case 'l':
            if (toneModesList != Q_NULLPTR)
                delete toneModesList;
            toneModesList = new tableCombobox(createModel(toneModesModel, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneMode, toneModesList);

            ui->table->showColumn(columnToneMode);
            visibleRows++;
            break;
        case 'L':
            if (toneModesListB != Q_NULLPTR)
                delete toneModesListB;
            toneModesListB = new tableCombobox(createModel(toneModesModelB, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneModeB, toneModesListB);

            ui->table->showColumn(columnToneModeB);
            visibleRows++;
            break;
        case 'm':
            if (dsqlList != Q_NULLPTR)
                delete dsqlList;
            dsqlList = new tableCombobox(createModel(dsqlModel, dsql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDSQL, dsqlList);

            ui->table->showColumn(columnDSQL);
            visibleRows++;
            break;
        case 'M':
            if (dsqlListB != Q_NULLPTR)
                delete dsqlListB;
            dsqlListB = new tableCombobox(createModel(dsqlModelB, dsql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDSQLB, dsqlListB);

            ui->table->showColumn(columnDSQLB);
            visibleRows++;
            break;
        case 'n':
            if (tonesList != Q_NULLPTR)
                delete tonesList;
            tonesList = new tableCombobox(createModel(tonesModel, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnTone, tonesList);

            ui->table->showColumn(columnTone);
            visibleRows++;
            break;
        case 'N':
            if (tonesListB != Q_NULLPTR)
                delete tonesListB;
            tonesListB = new tableCombobox(createModel(tonesModelB, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneB, tonesListB);

            ui->table->showColumn(columnToneB);
            visibleRows++;
            break;
        case 'o':
            if (tsqlList != Q_NULLPTR)
                delete tsqlList;
            tsqlList = new tableCombobox(createModel(tsqlModel, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnTSQL, tsqlList);

            ui->table->showColumn(columnTSQL);
            visibleRows++;
            break;
        case 'O':
            if (tsqlListB != Q_NULLPTR)
                delete tsqlListB;
            tsqlListB = new tableCombobox(createModel(tsqlModelB, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnTSQLB, tsqlListB);

            ui->table->showColumn(columnTSQLB);
            visibleRows++;
            break;
        case 'p':
            if (dtcspList != Q_NULLPTR)
                delete dtcspList;
            dtcspList = new tableCombobox(createModel(dtcspModel, dtcsp),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCSPolarity, dtcspList);

            ui->table->showColumn(columnDTCSPolarity);
            visibleRows++;
            break;
        case 'P':
            if (dtcspListB != Q_NULLPTR)
                delete dtcspListB;
            dtcspListB = new tableCombobox(createModel(dtcspModelB, dtcsp),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCSPolarityB, dtcspListB);

            ui->table->showColumn(columnDTCSPolarityB);
            visibleRows++;
            break;
        case 'q':
            if (dtcsList != Q_NULLPTR)
                delete dtcsList;
            dtcsList = new tableCombobox(createModel(dtcsModel, dtcs),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCS, dtcsList);

            ui->table->showColumn(columnDTCS);
            visibleRows++;
            break;
        case 'Q':
            if (dtcsListB != Q_NULLPTR)
                delete dtcsListB;
            dtcsListB = new tableCombobox(createModel(dtcsModel, dtcs),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCSB, dtcsList);

            ui->table->showColumn(columnDTCSB);
            visibleRows++;
            break;
        case 'r':
            if (dvsqlEditor != Q_NULLPTR)
                delete dvsqlEditor;
            dvsqlEditor = new tableEditor(QRegularExpression("[0-9]{0,2}"),ui->table);
            ui->table->setItemDelegateForColumn(columnDVSquelch, dvsqlEditor);

            ui->table->showColumn(columnDVSquelch);
            visibleRows++;
            break;
        case 'R':
            if (dvsqlEditorB != Q_NULLPTR)
                delete dvsqlEditorB;
            dvsqlEditorB = new tableEditor(QRegularExpression("[0-9]{0,2}"),ui->table);
            ui->table->setItemDelegateForColumn(columnDVSquelchB, dvsqlEditorB);

            ui->table->showColumn(columnDVSquelchB);
            visibleRows++;
            break;
        case 's':
            if (offsetEditor != Q_NULLPTR)
                delete offsetEditor;
            offsetEditor = new tableEditor(QRegularExpression("[0-9.]{0,7}"),ui->table);
            ui->table->setItemDelegateForColumn(columnOffset, offsetEditor);

            ui->table->showColumn(columnOffset);
            visibleRows++;
            break;
        case 'S':
            if (offsetEditorB != Q_NULLPTR)
                delete offsetEditorB;
            offsetEditorB = new tableEditor(QRegularExpression("[0-9.]{0,7}"),ui->table);
            ui->table->setItemDelegateForColumn(columnOffsetB, offsetEditorB);

            ui->table->showColumn(columnOffsetB);
            visibleRows++;
            break;
        case 't':
            if (urEditor != Q_NULLPTR)
                delete urEditor;
            urEditor = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
            ui->table->setItemDelegateForColumn(columnUR, urEditor);

            ui->table->showColumn(columnUR);
            visibleRows++;
            break;
        case 'T':
            if (urEditorB != Q_NULLPTR)
                delete urEditorB;
            urEditorB = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
            ui->table->setItemDelegateForColumn(columnURB, urEditorB);

            ui->table->showColumn(columnURB);
            visibleRows++;
            break;
        case 'u':
            if (r1Editor != Q_NULLPTR)
                delete r1Editor;
            r1Editor = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
            ui->table->setItemDelegateForColumn(columnR1, r1Editor);

            ui->table->showColumn(columnR1);
            visibleRows++;
            break;
        case 'U':
            if (r1EditorB != Q_NULLPTR)
                delete r1EditorB;
            r1EditorB = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
            ui->table->setItemDelegateForColumn(columnR1B, r1EditorB);

            ui->table->showColumn(columnR1B);
            visibleRows++;
            break;
        case 'v':
            if (r2Editor != Q_NULLPTR)
                delete r2Editor;
            r2Editor = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
            ui->table->setItemDelegateForColumn(columnR2, r2Editor);

            ui->table->showColumn(columnR2);
            visibleRows++;
            break;
        case 'V':
            if (r2EditorB != Q_NULLPTR)
                delete r2EditorB;
            r2EditorB = new tableEditor(QRegularExpression("[0-9A-Z\\/\\ ]{0,8}$"),ui->table);
            ui->table->setItemDelegateForColumn(columnR2B, r2EditorB);

            ui->table->showColumn(columnR2B);
            visibleRows++;
            break;
        case 'z':
            if (nameEditor != Q_NULLPTR)
                delete nameEditor;
            nameEditor = new tableEditor(QRegularExpression(QString("[0-9A-Za-z\\/\\ .-_^#]{0,%0}$").arg(parse.len)),ui->table);
            ui->table->setItemDelegateForColumn(columnName, nameEditor);

            ui->table->showColumn(columnName);
            visibleRows++;
            break;
        default:
            break;
        }
    }

    if (visibleRows > 15) {
        ui->table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    }
    else
    {
        ui->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }

    if (ui->group->currentData().toInt() == 200) {

          lastMemoryRequested = rigCaps.memStart;
          emit getSatMemory(lastMemoryRequested);
    } else {
        // Is the current group attached to a particular band?
        foreach (auto band, rigCaps.bands)
        {
            if (band.memGroup==ui->group->currentData().toInt())
            {
                emit setBand(band.band);
            }
        }

        lastMemoryRequested = quint32((ui->group->currentData().toInt())<<16) | (rigCaps.memStart & 0xffff);
        emit getMemory(lastMemoryRequested);
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
    // First, do we need to request the next memory?
    if ((lastMemoryRequested & 0xffff) < groupMemories)
    {
        lastMemoryRequested++;
        if (mem.sat)
            emit getSatMemory(lastMemoryRequested);
        else
            emit getMemory(lastMemoryRequested);
        timeoutTimer.start(MEMORY_TIMEOUT);
    }
    else if (mem.channel == groupMemories)
    {
        timeoutTimer.stop();
        ui->group->setEnabled(true);
        ui->loadingMemories->setVisible(false);
        ui->table->setEditTriggers(QAbstractItemView::AllEditTriggers);
    }

    timeoutCount=0; // We have received a memory, so set the timeout to zero.
    int validData=0;

    // Now process the incoming memory
    int row=-1;

    for (int n = 0; n<ui->table->rowCount();n++)
    {
        if (ui->table->item(n,columnNum) != NULL && ui->table->item(n,columnNum)->text().toInt() == mem.channel && mem.group == ui->group->currentData().toInt()) {

            row = n;
            break;
        }
    }

    if (mem.scan < 4) {
        ui->table->blockSignals(true);

        if (row == -1) {
            ui->table->insertRow(ui->table->rowCount());
            row=ui->table->rowCount()-1;
            QPushButton* recall = new QPushButton("Recall");
            validData++;
            ui->table->setCellWidget(row,columnRecall,recall);
            connect(recall, &QPushButton::clicked, this,
                    [=]() { qInfo() << "Recalling" << mem.channel; emit recallMemory((quint32((ui->group->currentData().toUInt() << 16) | mem.channel)));});
        }

        ui->table->model()->setData(ui->table->model()->index(row,columnNum),QString::number(mem.channel & 0xffff).rightJustified(3,'0'));
        ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() & (~Qt::ItemIsEditable));
        ui->table->item(row,columnNum)->setBackground(Qt::transparent);
        validData++;


        // Set every combobox to default value first:
/*
        if (ui->table->item(row,columnSplit) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnSplit),split[0]);
        if (ui->table->item(row,columnScan) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnScan),scan[0]);
        if (ui->table->item(row,columnFilter) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnFilter),filters[0]);
        if (ui->table->item(row,columnFilterB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnFilterB),filters[0]);
        if (ui->table->item(row,columnData) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnData),dataModes[0]);
        if (ui->table->item(row,columnDuplex) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDuplex),duplexModes[0]);
        if (ui->table->item(row,columnDuplexB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDuplexB),duplexModes[0]);
        if (ui->table->item(row,columnToneMode) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnToneMode),toneModes[0]);
        if (ui->table->item(row,columnDSQL) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDSQL),dsql[0]);
        if (ui->table->item(row,columnTone) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnTone),tones[0]);
        if (ui->table->item(row,columnTSQL) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnTSQL),tones[0]);
        if (ui->table->item(row,columnDTCSPolarity) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCSPolarity),dtcsp[0]);
        if (ui->table->item(row,columnDTCS) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCS),dtcs[0]);
        if (ui->table->item(row,columnFilterB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnFilterB),filters[0]);
        if (ui->table->item(row,columnDataB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDataB),dataModes[0]);
        if (ui->table->item(row,columnToneModeB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnToneModeB),toneModes[0]);
        if (ui->table->item(row,columnDSQLB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDSQLB),dsql[0]);
        if (ui->table->item(row,columnToneB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnToneB),tones[0]);
        if (ui->table->item(row,columnTSQLB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnTSQLB),tones[0]);
        if (ui->table->item(row,columnDTCSPolarityB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCSPolarityB),dtcsp[0]);
        if (ui->table->item(row,columnDTCSB) == NULL) ui->table->model()->setData(ui->table->model()->index(row,columnDTCSB),dtcs[0]);
*/

        for (int f=0;f<split.size();f++)
        {
            if (f==mem.split) {
                ui->table->model()->setData(ui->table->model()->index(row,columnSplit),split[f]);
                validData++;
                break;
            }
        }        

        for (int f=0;f<scan.size();f++)
        {
            if (f==mem.scan) {
                ui->table->model()->setData(ui->table->model()->index(row,columnScan),scan[f]);
                validData++;
                break;
            }
        }

        ui->table->model()->setData(ui->table->model()->index(row,columnFrequency),QString::number(double(mem.frequency.Hz/1000000.0),'f',3));
        validData++;

        ui->table->model()->setData(ui->table->model()->index(row,columnFrequencyB),QString::number(double(mem.frequencyB.Hz/1000000.0),'f',3));
        validData++;

        foreach (auto mode, rigCaps.modes){
            if (mode.reg == mem.mode) {
                ui->table->model()->setData(ui->table->model()->index(row,columnMode),mode.name);
                validData++;
            }
            if (mode.reg == mem.modeB) {
                ui->table->model()->setData(ui->table->model()->index(row,columnModeB),mode.name);
                validData++;
            }
        }

        for (int f=0;f<dataModes.size();f++)
        {
            if (f==mem.datamode) {
                ui->table->model()->setData(ui->table->model()->index(row,columnData),dataModes[f]);
                validData++;
            }
            if (f==mem.datamodeB) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDataB),dataModes[f]);
                validData++;
            }
        }

        for (int f=0;f<filters.size();f++)
        {
            if (f==mem.filter-1) {
                ui->table->model()->setData(ui->table->model()->index(row,columnFilter),filters[f]);
                validData++;
            }
            if (f==mem.filterB-1) {
                ui->table->model()->setData(ui->table->model()->index(row,columnFilterB),filters[f]);
                validData++;
            }
        }

        for (quint8 f=0;f<duplexModes.size();f++)
        {
            if (f==mem.duplex) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDuplex),duplexModes[f]);
                validData++;
            }
            if (f==mem.duplexB) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDuplexB),duplexModes[f]);
                validData++;
            }
        }

        for (int f=0;f<toneModes.size();f++)
        {
            if (f==mem.tonemode) {
                ui->table->model()->setData(ui->table->model()->index(row,columnToneMode),toneModes[f]);
                validData++;
            }
            if (f==mem.tonemodeB) {
                ui->table->model()->setData(ui->table->model()->index(row,columnToneModeB),toneModes[f]);
                validData++;
            }
        }

        for (int f=0;f<dsql.size();f++)
        {
            if (f==mem.dsql) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDSQL),dsql[f]);
                validData++;
            }
            if (f==mem.dsqlB) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDSQLB),dsql[f]);
                validData++;
            }
        }

        ui->table->model()->setData(ui->table->model()->index(row,columnTone),QString::number((float)mem.tone/10,'f',1));
        validData++;

        ui->table->model()->setData(ui->table->model()->index(row,columnToneB),QString::number((float)mem.toneB/10,'f',1));
        validData++;

        ui->table->model()->setData(ui->table->model()->index(row,columnTSQL),QString::number((float)mem.tsql/10,'f',1));
        validData++;

        ui->table->model()->setData(ui->table->model()->index(row,columnTSQLB),QString::number((float)mem.tsqlB/10,'f',1));
        validData++;

        ui->table->model()->setData(ui->table->model()->index(row,columnDVSquelch),QString::number(mem.dvsql).rightJustified(2,'0'));
        validData++;

        ui->table->model()->setData(ui->table->model()->index(row,columnDVSquelchB),QString::number(mem.dvsqlB).rightJustified(2,'0'));
        validData++;

        for (int f=0;f<dtcsp.size();f++)
        {
            if (f==mem.dtcsp) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDTCSPolarity),dtcsp[f]);
                validData++;
            }
            if (f==mem.dtcspB) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDTCSPolarityB),dtcsp[f]);
                validData++;
            }
        }

        for (int f=0;f<dtcs.size();f++)
        {
            if (dtcs[f].toInt()==mem.dtcs) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDTCS),dtcs[f]);
                validData++;
            }
            if (dtcs[f].toInt()==mem.dtcsB) {
                ui->table->model()->setData(ui->table->model()->index(row,columnDTCSB),dtcs[f]);
                validData++;
            }
        }

        ui->table->model()->setData(ui->table->model()->index(row,columnOffset),QString::number(double(mem.duplexOffset.Hz/10000.0),'f',3));
        validData++;

        ui->table->model()->setData(ui->table->model()->index(row,columnOffsetB),QString::number(double(mem.duplexOffsetB.Hz/10000.0),'f',3));
        validData++;

        if (checkASCII(mem.UR)) {
            ui->table->model()->setData(ui->table->model()->index(row,columnUR),QString(mem.UR));
            validData++;
        } else
            qInfo() << "Invalid data in ur";

        if (checkASCII(mem.URB)) {
            ui->table->model()->setData(ui->table->model()->index(row,columnURB),QString(mem.URB));
            validData++;
        } else
            qInfo() << "Invalid data in urb";


        if (checkASCII(mem.R1)) {
            ui->table->model()->setData(ui->table->model()->index(row,columnR1),QString(mem.R1));
            validData++;
        } else
            qInfo() << "Invalid data in r1";


        if (checkASCII(mem.R1B)) {
            ui->table->model()->setData(ui->table->model()->index(row,columnR1B),QString(mem.R1B));
            validData++;
        } else
            qInfo() << "Invalid data in r1b";


        if (checkASCII(mem.R2)) {
            ui->table->model()->setData(ui->table->model()->index(row,columnR2),QString(mem.R2));
            validData++;
        } else
            qInfo() << "Invalid data in r2";


        if (checkASCII(mem.R2B)) {
            ui->table->model()->setData(ui->table->model()->index(row,columnR2B),QString(mem.R2B));
            validData++;
        } else
            qInfo() << "Invalid data in r2b";


        if (checkASCII(mem.name)) {
            ui->table->model()->setData(ui->table->model()->index(row,columnName),QString(mem.name));
            validData++;
        } else
            qInfo() << "Invalid data in name";


        ui->table->blockSignals(false);

        int visibleColumns=0;

        for (int i=0;i<ui->table->columnCount();i++)
        {
            if (!ui->table->isColumnHidden(i))
                visibleColumns++;
        }

        if (validData < visibleColumns-1) {
            qInfo(logRig()) << "Memory" << mem.channel << "Received valid data for" << validData << "columns, " << "expected" << visibleColumns << "requesting again";
            if (mem.sat)
                emit getSatMemory(mem.channel & 0xffff);
            else
                emit getMemory(quint32((ui->group->currentData().toInt())<<16) | (mem.channel & 0xffff));
        }

    }
    else if (row != -1)
    {
        // Check if we already have this memory as it might have failed to write?
        ui->loadingMemories->setStyleSheet("QLabel {color: #ff0000}");
        ui->table->item(row,columnNum)->setBackground(Qt::red);
    }

}

bool memories::checkASCII(QString str)
{
    static QRegularExpression exp = QRegularExpression(QStringLiteral("[^\\x{0000}-\\x{007F}]"));
    bool containsNonASCII = str.contains(exp);
    return !containsNonASCII;
}


void memories::timeout()
{
    if (timeoutCount < 10 )
    {
        qInfo(logRig()) << "Timeout receiving memory:" << (lastMemoryRequested & 0xffff) << "in group" << (lastMemoryRequested >> 16 & 0xffff);
        if (ui->group->currentData().toInt() == 200)
            emit getSatMemory(lastMemoryRequested);
        else
            emit getMemory(lastMemoryRequested);
        timeoutTimer.start(MEMORY_TIMEOUT);
        timeoutCount++;
    } else {
        timeoutCount=0;
        ui->loadingMemories->setVisible(false);
        timeoutTimer.stop();
        ui->group->setEnabled(true);
        ui->table->setEditTriggers(QAbstractItemView::AllEditTriggers);

        QMessageBox::information(this,"Timeout", "Timeout receiving memories, check rig connection", QMessageBox::Ok);
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


void memories::on_csvImport_clicked()
{
    QString file = QFileDialog::getOpenFileName(this,"Select import filename","","CSV Files (*.csv)");

    if (!file.isEmpty())
    {
        ui->table->sortByColumn(0,Qt::AscendingOrder); // Force natural order

        QFile data(file);
        if (!data.open(QIODevice::ReadOnly)) {
            qInfo() << "Couldn't open file for .csv import" << data.errorString();
            return;
        }

        int lastcol=0;

        for (int i=0;i<ui->table->columnCount();i++)
        {
            if (!ui->table->isColumnHidden(i))
                lastcol=i;
        }

        QTextStream input(&data);
        QStringList row;
        int rows=0;
        while (readCSVRow(input, &row)) {
            qInfo() << row;
            if (!rows++)
                continue; // Skip the first row

            int rownum = -1;
            for(int i=0;i<ui->table->rowCount();i++) {
                if (ui->table->item(i, 1)->text().toInt() == row[0].toInt()) {
                    rownum=i;
                    break;
                }
            }
            if (rownum == -1)
            {
                // We need to add a new row
                rownum = ui->table->rowCount();
                ui->table->insertRow(rownum);
                QPushButton* recall = new QPushButton("Recall");
                ui->table->setCellWidget(rownum,columnRecall,recall);
                connect(recall, &QPushButton::clicked, this,
                        [=]() { qInfo() << "Recalling" << row[0].toInt(); emit recallMemory((quint32((ui->group->currentData().toUInt() << 16) | row[0].toInt())));});
            }
            // rownum is now the row we need to work on.

            int colnum=1;

            ui->table->blockSignals(true);
            for (int i=0; i<row.size();i++)
            {
                while (colnum < ui->table->columnCount() && ui->table->isColumnHidden(colnum)) {
                    colnum++;
                }

                // Stop blocking signals for last column (should force it to be sent to rig)

                if (colnum < ui->table->columnCount())
                {
                    QString data = row[i];
                    for (int n= data.size(); n<=0; --n)
                    {
                        if (!data.at(n).isSpace()) {
                            data.truncate(n+1);
                            break;
                        }
                    }
                    switch (colnum)
                    {
                    // special cases:
                    case columnFrequency:
                    case columnFrequencyB:
                        data= QString::number(data.toDouble(),'f',3);
                        break;
                    case columnDTCS:
                    case columnDTCSB:
                        data=QString::number(data.toInt()).rightJustified(3,'0');
                        break;
                    case columnDVSquelch:
                    case columnDVSquelchB:
                        data=QString::number(data.toInt()).rightJustified(2,'0');
                        break;
                    case columnTone:
                    case columnToneB:
                    case columnTSQL:
                    case columnTSQLB:
                        if (data.endsWith("Hz")) data=data.mid(0,data.length()-2);
                        break;
                    case columnFilter:
                    case columnFilterB:
                        if (!data.startsWith("FIL")) data = "FIL"+data;
                        break;
                    default:
                        break;
                    }

                    if (colnum == lastcol)
                        ui->table->blockSignals(false);

                    ui->table->model()->setData(ui->table->model()->index(rownum,colnum),data);
                    colnum++;
                }
            }
        }
    }
}

void memories::on_csvExport_clicked()
{
    QString file = QFileDialog::getSaveFileName(this,"Select export filename","","CSV Files (*.csv)");

    if (!file.isEmpty())
    {
        ui->table->sortByColumn(0,Qt::AscendingOrder); // Force natural order

        QFile data(file);
        if (data.open(QFile::WriteOnly | QIODevice::Truncate)) {
            QTextStream output(&data);
            for(int i=1;i<ui->table->columnCount();i++) {
                if (!ui->table->isColumnHidden(i))
                {
                    output << "\"" << ui->table->horizontalHeaderItem(i)->text() << "\"";
                    if (i < ui->table->columnCount()-1)
                        output << ",";
                    else
                        output << "\n";
                }
            }

            for(int i=0;i<ui->table->rowCount();i++) {
                for(int j=1;j<ui->table->columnCount();j++) {
                    if (!ui->table->isColumnHidden(j))
                    {
                        output << "\"" << ((ui->table->item(i,j) == NULL) ? "" : ui->table->item(i,j)->text()) << "\"";
                        if (j < ui->table->columnCount()-1)
                            output << ",";
                        else
                            output << "\n";
                    }
                }
            }
        }
        data.close();
    }
}

// Public Domain CSV parser from user iamantony
// https://www.appsloveworld.com/cplus/100/37/parsing-through-a-csv-file-in-qt
bool memories::readCSVRow(QTextStream &in, QStringList *row) {

    static const int delta[][5] = {
        //  ,    "   \n    ?  eof
        {   1,   2,  -1,   0,  -1  }, // 0: parsing (store char)
        {   1,   2,  -1,   0,  -1  }, // 1: parsing (store column)
        {   3,   4,   3,   3,  -2  }, // 2: quote entered (no-op)
        {   3,   4,   3,   3,  -2  }, // 3: parsing inside quotes (store char)
        {   1,   3,  -1,   0,  -1  }, // 4: quote exited (no-op)
        // -1: end of row, store column, success
        // -2: eof inside quotes
    };

    row->clear();

    if (in.atEnd())
        return false;

    int state = 0, t;
    char ch;
    QString cell;

    while (state >= 0) {

        if (in.atEnd())
            t = 4;
        else {
            in >> ch;
            if (ch == ',') t = 0;
            else if (ch == '\"') t = 1;
            else if (ch == '\n') t = 2;
            else t = 3;
        }

        state = delta[state][t];

        switch (state) {
        case 0:
        case 3:
            cell += ch;
            break;
        case -1:
        case 1:
            row->append(cell);
            cell = "";
            break;
        }

    }

    if (state == -2) {
        qInfo() << "End-of-file found while inside quotes.";
        return false;
    }
    return true;

}
