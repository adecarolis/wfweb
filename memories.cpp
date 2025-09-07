#include <QDebug>
#include "logcategories.h"

#include "memories.h"
#include "ui_memories.h"


memories::memories(bool isAdmin, bool slowLoad, QWidget *parent) :
    QWidget(parent),
    slowLoad(slowLoad),
    ui(new Ui::memories)
{
    ui->setupUi(this);
    ui->table->setColumnCount(totalColumns);
    ui->table->editing(false);
    statusBar = new QStatusBar(this);
    ui->verticalLayout->addWidget(statusBar);
    ui->csvImport->setEnabled(false);

    progress = new QProgressBar(this);
    statusBar->addWidget(progress,1);
    this->setObjectName("memories");
    queue = cachingQueue::getInstance();
    rigCaps = queue->getRigCaps();
    if (rigCaps == Q_NULLPTR)
    {
        qInfo() << "We have no rigCaps, so cannot continue";
        return;
    }

    if (rigCaps->manufacturer == manufKenwood || rigCaps->manufacturer == manufYaesu) {
        if (rigCaps->commands.contains(funcMemoryWrite))
            writeCommand = funcMemoryWrite;
        if (rigCaps->commands.contains(funcMemorySelect))
            selectCommand = funcMemorySelect;
    }

    if (!isAdmin)
    {
        ui->disableEditing->setEnabled(false);

    }


    progress->setRange(rigCaps->memStart,rigCaps->memories);

    if (!rigCaps->commands.contains(funcScanning))
    {
        ui->scanButton->setVisible(false);
    } else {
        ui->scanButton->setCheckable(true);
    }
    QStringList headers;

    /*

columnRecall=0, columnNum,columnSplit,columnSkip,columnScan,columnFrequency,columnClarOffset, columnRXClar, columnTXClar columcolumnMode,columnFilter,columnData,columnDuplex,
columnToneMode,columnTuningStep, columnCustomTuningStep, columnAttenuator, columnPreamplifier, columnAntenna, columnIPPlus,
columnDSQL,columnTone,columnTSQL,columnDTCS,columnDTCSPolarity,columnDVSquelch,columnOffset,columnUR,columnR1,columnR2,
columnP25Sql,columnP25Nac,columnDPmrSql,columnDPmrComid,columnDPmrCc,columnDPmrSCRM,columnDPmrKey,columnNxdnSql,
columnNnxdnRan,columnNxdnEnc,columnNxdnKey,columnDcrSql,columnDcrUc,columnDcrEnc,columnDcrKey,columnFrequencyB,
columnModeB,columnFilterB,columnDataB,columnDuplexB,columnToneModeB,columnDSQLB,columnToneB,columnTSQLB,columnDTCSB,
columnDTCSPolarityB,columnDVSquelchB,columnOffsetB,columnURB,columnR1B,columnR2B,totalColumns,
     */

    headers << "" << "Num" << "Name" << "Split" << "Skip" << "Scan" << "VFO" << "Freq" << "Clarifier" << "Clar RX" << "Clar TX" << "Mode" << "Filter" << "Data" <<"Duplex" << "Tn Mode" <<
        "Step" << "Prog Step" << "Atten" << "Preamp" << "Ant" << "IP Plus" << "DSQL" << "Tone" << "TSQL" << "DTCS" << "DTCS Pol" << "DV Sql" <<
        "Offset" << "UR" << "R1" << "R2" << "P25 SQL" << "P25 NAC" << "dPMR SQL" << "dPMR COM ID" << "dPMR CC" << "dPMR SCRM" << "dPMR Key" <<
        "NXDN SQL" << "NXDN RAN" << "NXDN Enc" << "NXDN Key" << "DCR SQL" << "DCR UC" << "DCR Enc"  << "DCR Key" <<
        "VFO B" << "Freq B" << "Mode B" << "Filter B" << "Data B" << "Duplex B" << "Tn Mode B" << "DSQL B" <<
        "Tone B" << "TSQL B" << "DTCS B" << "DTCSP B" <<
        "DV Sql B" << "Offset B" << "UR B" << "R1 B" << "R2 B";

    clar << "OFF" << "ON";

    skip << "OFF" << "SKIP" << "PSKIP";

    dataModes << "OFF" << "DATA1";


    if (rigCaps->commands.contains(funcDATA2Mod))
        dataModes.append("DATA2");
    if (rigCaps->commands.contains(funcDATA3Mod))
        dataModes.append("DATA3");

    duplexModes << "OFF" << "DUP-" << "DUP+" << "RPS";

    if (rigCaps->manufacturer == manufKenwood || rigCaps->manufacturer == manufYaesu) {
        scan << "VFO" << "MEM" << "TUNE" << "QMB" << "NONE" << "PMS";
        if (rigCaps->commands.contains(funcMemoryWrite))
            writeCommand = funcMemoryWrite;
        if (rigCaps->commands.contains(funcMemorySelect))
            selectCommand = funcMemorySelect;
        if (rigCaps->manufacturer == manufYaesu)
            toneModes << "OFF" << "ENC/DEC" << "ENC";
        else
            toneModes << "OFF" << "TONE" << "CTCSS";
        filters << "FILTER A" << "FILTER B";
        split << "SIMP" << "PLUS" << "MINUS";
    } else {
        scan << "OFF" << "*1" << "*2" << "*3";
        if (rigCaps->hasTransmit)
            toneModes << "OFF" << "TONE" << "TSQL";
        else
            toneModes << "OFF" << "TSQL";
        filters << "FIL1" << "FIL2" << "FIL3";
        split << "OFF" << "ON";
    }

    if (rigCaps->commands.contains(funcRepeaterDTCS))
    {
        toneModes.append("DTCS"); // 03
        toneModes.append("");
        toneModes.append("");
        toneModes.append("DTCS(T)");
        toneModes.append("TONE(T)/DTCS(R)");
        toneModes.append("DTCS(T)/TSQL(R)");
        toneModes.append("TONE(T)/TSQL(R)");
    }

    for (const auto&t : rigCaps->ctcss)
    {
        tones.append(t.name);
    }

    for (const auto&t : rigCaps->dtcs)
    {
        dtcs.append(t.name);
    }

    dsql << "OFF" << "DSQL" << "CSQL";
    p25Sql << "OFF" << "NAC";
    dPmrSql << "OFF" << "COM ID" << "CC";
    dPmrSCRM << "OFF" << "ON";
    nxdnSql << "OFF" << "ON";
    nxdnEnc << "OFF" << "ON";
    dcrSql << "OFF" << "UC";
    dcrEnc << "OFF" << "ON";

    if (rigCaps->hasTransmit)
        dtcsp << "BOTH N" << "N/R" << "R/N" << "BOTH R";
    else
        dtcsp << "NORMAL" << "REVERSE";


    ipplus << "OFF" << "ON";

    // We can populate the other comboboxes from RigCaps (ensuring they are valid)
    tuningSteps << "None";
    for (const auto &step: rigCaps->steps)
    {
        if (step.num)
        {
            tuningSteps.append(step.name);
        }
    }

    for (const auto &atten: rigCaps->attenuators) {
        attenuators.append(atten.name);
    }
    if (attenuators.isEmpty()) attenuators.append("None");

    for (const auto &preamp: rigCaps->preamps) {
        preamps.append(preamp.name);
    }
    if (preamps.isEmpty()) preamps.append("None");

    for (const auto &ant: rigCaps->antennas) {
        antennas.append(ant.name);
    }
    if (antennas.isEmpty()) antennas.append("None");

    ui->table->setHorizontalHeaderLabels(headers);

    ui->group->hide();
    //ui->modeButton->hide();
    ui->modeButton->setCheckable(true);
    //ui->loadingMemories->setVisible(false);
    //ui->loadingMemories->setStyleSheet("QLabel {color: #ff0000}");

    ui->group->blockSignals(true);
    ui->group->addItem("Memory Group",-1);
    // Set currentIndex to an invalid value to force an update.
    ui->group->setCurrentIndex(-1);
    for (int i=rigCaps->memStart;i<=rigCaps->memGroups;i++) {
        if (i == rigCaps->memStart) {
            // Disable title if any groups to stop it being selected.
            auto* model = qobject_cast<QStandardItemModel*>(ui->group->model());
            model->item(0)->setEnabled(false);
        }
        ui->group->addItem(QString("Group %0").arg(i,2,10,QChar('0')),i);
    }

    if (rigCaps->satMemories && rigCaps->commands.contains(funcSatelliteMode)){
        ui->group->addItem("Satellite",MEMORY_SATGROUP);
    }

    ui->group->blockSignals(false);

    for (int i=0;i<100;i++)
    {
        dvsql.append(QString::number(i).rightJustified(2,'0'));
    }

    if (rigCaps->commands.contains(funcVFOEqualAB)) {
        vfos << "VFOA" << "VFOB";
    } else if (rigCaps->commands.contains(funcVFOEqualMS)) {
        vfos << "MAIN" << "SUB";
    }

    for (auto &mode: rigCaps->modes){
        modes.append(mode.name);
    }
    if (modes.isEmpty()) modes.append("None");

    connect(ui->table,SIGNAL(rowAdded(int)),this,SLOT(rowAdded(int)));
    connect(ui->table,SIGNAL(rowValDeleted(quint32)),this,SLOT(rowDeleted(quint32)));
    connect(ui->table,SIGNAL(menuAction(QAction*, quint32)),this,SLOT(menuAction(QAction*, quint32)));
    connect(&timeoutTimer, SIGNAL(timeout()), this, SLOT(timeout()));

    ui->table->sortByColumn(columnRecall,Qt::AscendingOrder);

    ui->table->addAction(useCurrent);
    ui->table->addAction(addCurrent);
}

void memories::menuAction(QAction* action, quint32 row)
{
    // First we need to populate a memory struct:

    memoryType mem;
    vfoCommandType ta = queue->getVfoCommand(vfoA,0,false);
    vfoCommandType tb;

    if (rigCaps->numReceiver > 1)
        tb = queue->getVfoCommand(vfoA,1,false);
    else
        tb = queue->getVfoCommand(vfoB,0,false);

    mem.split = queue->getCache(funcSplitStatus).value.value<uchar>();

    mem.frequency = queue->getCache(ta.freqFunc,ta.receiver).value.value<freqt>();
    mem.mode = queue->getCache(ta.modeFunc,ta.receiver).value.value<modeInfo>().reg;
    mem.datamode = queue->getCache(ta.modeFunc,ta.receiver).value.value<modeInfo>().data;
    mem.filter = queue->getCache(ta.modeFunc,ta.receiver).value.value<modeInfo>().filter;
    mem.tonemode = queue->getCache(funcRepeaterTone,ta.receiver).value.value<uchar>();
    mem.tone = queue->getCache(funcToneFreq,ta.receiver).value.value<toneInfo>().name;
    mem.tsql = queue->getCache(funcTSQLFreq,ta.receiver).value.value<toneInfo>().name;
    mem.dtcs = queue->getCache(funcDTCSCode,ta.receiver).value.value<ushort>();
    mem.dvsql = queue->getCache(funcCSQLCode,ta.receiver).value.value<ushort>();

    mem.frequencyB = queue->getCache(tb.freqFunc,tb.receiver).value.value<freqt>();
    mem.modeB = queue->getCache(tb.modeFunc,tb.receiver).value.value<modeInfo>().reg;
    mem.datamodeB = queue->getCache(tb.modeFunc,tb.receiver).value.value<modeInfo>().data;
    mem.filterB = queue->getCache(tb.modeFunc,tb.receiver).value.value<modeInfo>().filter;
    mem.tonemodeB = queue->getCache(funcRepeaterTone,tb.receiver).value.value<uchar>();
    mem.toneB = queue->getCache(funcToneFreq,tb.receiver).value.value<toneInfo>().name;
    mem.tsqlB = queue->getCache(funcTSQLFreq,tb.receiver).value.value<toneInfo>().name;
    mem.dtcsB = queue->getCache(funcDTCSCode,tb.receiver).value.value<ushort>();
    mem.dvsqlB = queue->getCache(funcCSQLCode,tb.receiver).value.value<ushort>();

    memset(mem.UR, 0x0, sizeof(mem.UR));
    memset(mem.URB, 0x0, sizeof(mem.URB));
    memset(mem.R1, 0x0, sizeof(mem.R1));
    memset(mem.R1B, 0x0, sizeof(mem.R1B));
    memset(mem.R2, 0x0, sizeof(mem.R2));
    memset(mem.R2B, 0x0, sizeof(mem.R2B));

    if (action == useCurrent)
    {
        qInfo() << "Use Current" << row;
        memcpy(mem.name,"<UPDATED>",10);
        updateRow(row,mem);
    }
    else if (action == addCurrent)
    {
        qInfo() << "Add Current" << row;
        memcpy(mem.name,"<NEW>",6);
        row=ui->table->rowCount();
        ui->table->insertRow(ui->table->rowCount());
        rowAdded(row,mem);
    }
    else
    {
        qInfo() << "Unknown action" << row;
    }
}

void memories::enableCell(int row, int col, bool en) {
    if (!ui->table->isColumnHidden(col) && ui->table->item(row,col) != NULL) {
        if (en)
            ui->table->item(row,col)->setFlags(ui->table->item(row,col)->flags() | Qt::ItemIsEnabled);
        else
            ui->table->item(row,col)->setFlags(ui->table->item(row,col)->flags() & ~Qt::ItemIsEnabled);
    }
}

void memories::populate()
{
    if (ui->group->count() > 1)
    {
        qInfo() << "Memory group items:" << ui->group->count();
        ui->group->setCurrentIndex(1);
    }
    else
    {
        ui->group->setCurrentIndex(0);
    }
}
memories::~memories()
{
    qInfo() << "Deleting memories table";
    ui->table->clear();
    delete ui;
}

int memories::updateRow(int row, memoryType mem, bool store)
{


    ui->table->blockSignals(true);

    int validData=0; // We have 1 more row than will ever be received.

    validData += updateEntry(split,row,columnSplit,mem.split);
    validData += updateEntry(skip,row,columnSkip,mem.skip);
    validData += updateEntry(scan,row,columnScan,mem.scan);

    ui->table->model()->setData(ui->table->model()->index(row,columnFrequency),QString::number(double(mem.frequency.Hz/1000000.0),'f',5));
    validData++;

    if (!rigCaps->commands.contains(funcSplitMemory)) {
        if (mem.frequencyB.Hz > 0)
            ui->table->model()->setData(ui->table->model()->index(row,columnFrequencyB),QString::number(double(mem.frequencyB.Hz/1000000.0),'f',5));
        else
            ui->table->model()->setData(ui->table->model()->index(row,columnFrequencyB),QString::number(double(mem.frequency.Hz/1000000.0),'f',5));
    }
    validData++;

    if (mem.clarifier > -10000 && mem.clarifier < 10000) {
        ui->table->model()->setData(ui->table->model()->index(row,columnClarOffset),QString("%1%2").arg(mem.clarifier < 0 ? '-' : '+').arg(abs(mem.clarifier),4,10,QChar('0')));
        validData++;
    }

    validData += updateEntry(clar,row,columnRXClar,mem.clarRX);
    validData += updateEntry(clar,row,columnTXClar,mem.clarTX);


    // Make sure these are populated before mode so they can be enabled/disabled.
    validData += updateEntry(toneModes,row,columnToneMode,mem.tonemode);
    validData += updateEntry(toneModes,row,columnToneModeB,mem.tonemodeB);

    validData += updateEntry(dsql,row,columnDSQL,mem.dsql);
    validData += updateEntry(dsql,row,columnDSQLB,mem.dsqlB);

    validData += updateEntry(tones,row,columnTSQL,mem.tsql);
    validData += updateEntry(tones,row,columnTSQLB,mem.tsqlB);

    validData += updateEntry(dvsql,row,columnDVSquelch,QString::number(mem.dvsql).rightJustified(2,'0'));
    validData += updateEntry(dvsql,row,columnDVSquelchB,QString::number(mem.dvsqlB).rightJustified(2,'0'));

    validData += updateEntry(dtcs,row,columnDTCS,mem.dtcs);
    validData += updateEntry(dtcs,row,columnDTCSB,mem.dtcsB);

    validData += updateEntry(dtcsp,row,columnDTCSPolarity,mem.dtcsp);
    validData += updateEntry(dtcsp,row,columnDTCSPolarityB,mem.dtcspB);

    validData += updateEntry(dataModes,row,columnData,mem.datamode);
    validData += updateEntry(dataModes,row,columnDataB,mem.datamodeB);

    if (rigCaps->manufacturer == manufKenwood) {
        validData += updateEntry(filters,row,columnFilter,mem.filter);
        validData += updateEntry(filters,row,columnFilterB,mem.filterB);
    } else {
        validData += updateEntry(filters,row,columnFilter,mem.filter-1);
        validData += updateEntry(filters,row,columnFilterB,mem.filterB-1);
    }
    validData += updateEntry(duplexModes,row,columnDuplex,mem.duplex);
    validData += updateEntry(duplexModes,row,columnDuplexB,mem.duplexB);

    validData += updateEntry(tones,row,columnTone,mem.tone);
    validData += updateEntry(tones,row,columnToneB,mem.toneB);

    validData += updateEntry(tuningSteps,row,columnTuningStep,mem.tuningStep);

    ui->table->model()->setData(ui->table->model()->index(row,columnCustomTuningStep),QString::number(mem.progTs));
    validData++;

    if (ui->table->item(row,columnTuningStep) != NULL && ui->table->item(row,columnTuningStep)->text() != "Prog")
    {
        ui->table->item(row,columnCustomTuningStep)->setFlags(ui->table->item(row,columnCustomTuningStep)->flags() & ~Qt::ItemIsEnabled);
    }

    validData += updateEntry(attenuators,row,columnAttenuator,mem.atten);
    validData += updateEntry(preamps,row,columnPreamplifier,mem.preamp);
    validData += updateEntry(antennas,row,columnAntenna,mem.antenna);
    validData += updateEntry(ipplus,row,columnIPPlus,mem.ipplus);

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


    if (!rigCaps->commands.contains(funcMemoryTag) && !rigCaps->commands.contains(funcMemoryTagB)) {
        if (checkASCII(mem.name)) {
            ui->table->model()->setData(ui->table->model()->index(row,columnName),QString(mem.name));
            validData++;
        } else {
            qInfo() << "Invalid data in name";
        }
    }

    if (!rigCaps->commands.contains(funcSplitMemory))
    {
        if (mem.frequencyB.Hz > 0)
            ui->table->model()->setData(ui->table->model()->index(row,columnFrequencyB),QString::number(double(mem.frequencyB.Hz/1000000.0),'f',5));
        else
            ui->table->model()->setData(ui->table->model()->index(row,columnFrequencyB),QString::number(double(mem.frequency.Hz/1000000.0),'f',5));
    }

    // These are optional so don't update validdata
    updateEntry(p25Sql,row,columnP25Sql,mem.p25Sql);
    ui->table->model()->setData(ui->table->model()->index(row,columnP25Nac),QString::number(mem.p25Nac,16).rightJustified(3,'0'));
    updateEntry(dPmrSql,row,columnDPmrSql,mem.dPmrSql);
    ui->table->model()->setData(ui->table->model()->index(row,columnDPmrComid),QString::number(mem.dPmrComid).rightJustified(3,'0'));
    ui->table->model()->setData(ui->table->model()->index(row,columnDPmrCc),QString::number(mem.dPmrCc).rightJustified(2,'0'));
    updateEntry(dPmrSCRM,row,columnDPmrSCRM,mem.dPmrSCRM);
    ui->table->model()->setData(ui->table->model()->index(row,columnDPmrKey),QString::number(mem.dPmrKey).rightJustified(5,'0'));
    updateEntry(nxdnSql,row,columnNxdnSql,mem.nxdnSql);
    ui->table->model()->setData(ui->table->model()->index(row,columnNxdnRan),QString::number(mem.nxdnRan).rightJustified(2,'0'));
    updateEntry(nxdnEnc,row,columnNxdnEnc,mem.nxdnEnc);
    ui->table->model()->setData(ui->table->model()->index(row,columnNxdnKey),QString::number(mem.nxdnKey).rightJustified(5,'0'));
    updateEntry(dcrSql,row,columnDcrSql,mem.dcrSql);
    ui->table->model()->setData(ui->table->model()->index(row,columnDcrUc),QString::number(mem.dcrUc).rightJustified(3,'0'));
    updateEntry(dcrEnc,row,columnDcrEnc,mem.dcrEnc);
    ui->table->model()->setData(ui->table->model()->index(row,columnDcrKey),QString::number(mem.dcrKey).rightJustified(5,'0'));

    // Do this after all other fields to ensure they have been populated.
    for (uint i=0;i<rigCaps->modes.size();i++)
    {
        if (mem.mode == rigCaps->modes[i].reg)
        {
            validData += updateEntry(modes,row,columnMode,i);
            // This mode is the one we are interested in!
            configColumns(row, rigCaps->modes[i],mem.split);
        }
        if (mem.modeB == rigCaps->modes[i].reg)
        {
            validData += updateEntry(modes,row,columnModeB,i);
            configColumnsB(row, rigCaps->modes[i]);
        }
    }

    ui->table->blockSignals(false);

    if (store)
    {
        on_table_cellChanged(row,columnNum); // Force an attempt to store this row.
    }

    return validData;
}

void memories::rowAdded(int row, memoryType mem)
{
    // Find the next available memory number:

    quint32 num=rigCaps->memStart;

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
            if ((ui->group->currentData().toInt() != 200 && rows.back() < groupMemories-1) || (ui->group->currentData().toInt() == MEMORY_SATGROUP && rows.back() < rigCaps->satMemories-1))
            {
                num = rows.back() + 1;
            }
            else if (rows.front() == rigCaps->memStart)
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

    connect(recall, &QPushButton::clicked, this, [=]() {
        recallMem(num);
    });

    updateEntry(row,columnNum,QString::number(num).rightJustified(3,'0'));

    updateRow(row,mem);
}

void memories::recallMem(quint32 num)
{
    qInfo() << "Recalling" << num;
    if (rigCaps->commands.contains(funcMemoryGroup)) {
        queue->add(priorityImmediate,queueItem(funcMemoryGroup,QVariant::fromValue<uchar>(quint16(ui->group->currentData().toUInt()))));
    } else {
        for (auto &band: rigCaps->bands)
        {
            if (band.region == "" || band.region == region) {
                if (ui->group->isVisible() && band.memGroup == ui->group->currentData().toInt())
                {
                    bandStackType bs;
                    bs.band = band.bsr;
                    bs.reg = 1;
                    queue->add(priorityImmediate,queueItem(funcBandStackReg, QVariant::fromValue<bandStackType>(bs),false,uchar(0)));
                    break;
                }
            }
        }
    }
    queue->add(priorityImmediate,queueItem(selectCommand,QVariant::fromValue<uint>(quint32((ui->group->currentData().toUInt() << 16) | num))));
    // We also should request the current frequency/mode etc so that the UI is updated.
    vfoCommandType t = queue->getVfoCommand(vfoA,0,false);
    queue->add(priorityImmediate,t.freqFunc,false,t.receiver);
    queue->add(priorityImmediate,t.modeFunc,false,t.receiver);
    if (ui->modeButton->isVisible() && !ui->modeButton->isChecked())
    {
        on_modeButton_clicked(true);
    }
}
void memories::rowDeleted(quint32 mem)
{
    if (mem >= rigCaps->memStart && mem <= rigCaps->memories) {
        qInfo() << "Mem Deleted" << mem;
        memoryType currentMemory;
        if (ui->group->currentData().toInt() == MEMORY_SATGROUP)
        {
            currentMemory.sat=true;
        }

        currentMemory.group=ui->group->currentData().toInt();
        currentMemory.channel = mem;
        currentMemory.del = true;
        queue->add(priorityImmediate,queueItem((currentMemory.sat?funcSatelliteMemory:writeCommand),QVariant::fromValue<memoryType>(currentMemory)));
    }
}
/* A cell has been updated, verify the data and send to the radio */
void memories::on_table_cellChanged(int row, int col)
{
    // If the import is updating a hidden column, ignore it.
    if (ui->table->isColumnHidden(col))
        return;

    memoryType currentMemory;
    currentMemory.group = ui->group->currentData().toInt();
    currentMemory.channel = (ui->table->item(row,columnNum) == NULL) ? 0 : ui->table->item(row,columnNum)->text().toInt();

    if (currentMemory.group == MEMORY_SATGROUP) {
        currentMemory.sat=true;
    }

    // The table shouldn't be updated below, simply queried for data.

    // We need to do this here to ensure we have the correct mode.
    for (auto &m: rigCaps->modes){
        if (!ui->table->isColumnHidden(columnModeB) && ui->table->item(row,columnModeB) != NULL && ui->table->item(row,columnModeB)->text()==m.name) {
            currentMemory.modeB=m.reg;
        }

        if (!ui->table->isColumnHidden(columnMode) && ui->table->item(row,columnMode) != NULL && ui->table->item(row,columnMode)->text()==m.name) {
            currentMemory.mode=m.reg;
        }
    }

    if (!ui->table->isColumnHidden(columnSplit) && ui->table->item(row,columnSplit) != NULL) {
        currentMemory.split = split.indexOf(ui->table->item(row,columnSplit)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnSkip) && ui->table->item(row,columnSkip) != NULL) {
        currentMemory.skip = skip.indexOf(ui->table->item(row,columnSkip)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnScan) && ui->table->item(row,columnScan) != NULL) {
        currentMemory.scan = scan.indexOf(ui->table->item(row,columnScan)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnVFO) && ui->table->item(row,columnVFO) != NULL) {
        currentMemory.vfo = vfos.indexOf(ui->table->item(row,columnVFO)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnVFOB) && ui->table->item(row,columnVFOB) != NULL) {
        currentMemory.vfoB = vfos.indexOf(ui->table->item(row,columnVFOB)->text().toUpper());
    }

    currentMemory.frequency.Hz = (ui->table->item(row,columnFrequency) == NULL) ? 0 : quint64(ui->table->item(row,columnFrequency)->text().toDouble()*1000000.0);
    currentMemory.frequencyB.Hz = (ui->table->item(row,columnFrequencyB) == NULL) ? 0 : quint64(ui->table->item(row,columnFrequencyB)->text().toDouble()*1000000.0);

    if (!ui->table->isColumnHidden(columnClarOffset) && ui->table->item(row,columnClarOffset) != NULL) {
        currentMemory.clarifier = (ui->table->item(row,columnClarOffset) == NULL) ? 0 : ui->table->item(row,columnClarOffset)->text().toShort();
        qInfo() << "Clarifier offset:" << ui->table->item(row,columnClarOffset)->text() << "now:" << currentMemory.clarifier;

    }

    if (!ui->table->isColumnHidden(columnRXClar) && ui->table->item(row,columnRXClar) != NULL) {
        currentMemory.clarRX = clar.indexOf(ui->table->item(row,columnRXClar)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnTXClar) && ui->table->item(row,columnTXClar) != NULL) {
        currentMemory.clarTX = clar.indexOf(ui->table->item(row,columnTXClar)->text().toUpper());
    }

    if (currentMemory.clarifier != 0)
    {
        //ClarRX or ClarTX MUST be enabled
        if (!currentMemory.clarRX && !currentMemory.clarTX)
        {
            currentMemory.clarRX=true;
            currentMemory.clarTX=true;
        }
    }

    if (!ui->table->isColumnHidden(columnData) && ui->table->item(row,columnData) != NULL) {
        currentMemory.datamode = dataModes.indexOf(ui->table->item(row,columnData)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnDataB) && ui->table->item(row,columnDataB) != NULL) {
        currentMemory.datamodeB = dataModes.indexOf(ui->table->item(row,columnDataB)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnFilter) && ui->table->item(row,columnFilter) != NULL) {
        if (rigCaps->manufacturer==manufKenwood)
            currentMemory.filter = filters.indexOf(ui->table->item(row,columnFilter)->text().toUpper());
        else
            currentMemory.filter = filters.indexOf(ui->table->item(row,columnFilter)->text().toUpper())+1;
    }

    if (!ui->table->isColumnHidden(columnFilterB) && ui->table->item(row,columnFilterB) != NULL) {
        if (rigCaps->manufacturer==manufKenwood)
            currentMemory.filterB = filters.indexOf(ui->table->item(row,columnFilterB)->text().toUpper());
        else
            currentMemory.filterB = filters.indexOf(ui->table->item(row,columnFilterB)->text().toUpper())+1;
    }

    if (!ui->table->isColumnHidden(columnDuplex) && ui->table->item(row,columnDuplex) != NULL) {
        currentMemory.duplex = duplexModes.indexOf(ui->table->item(row,columnDuplex)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnDuplexB) && ui->table->item(row,columnDuplexB) != NULL) {
        currentMemory.duplexB = duplexModes.indexOf(ui->table->item(row,columnDuplexB)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnToneMode) && ui->table->item(row,columnToneMode) != NULL) {
        currentMemory.tonemode = toneModes.indexOf(ui->table->item(row,columnToneMode)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnToneModeB) && ui->table->item(row,columnToneModeB) != NULL) {
        currentMemory.tonemodeB = toneModes.indexOf(ui->table->item(row,columnToneModeB)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnTuningStep) && ui->table->item(row,columnTuningStep) != NULL) {
        currentMemory.tuningStep = tuningSteps.indexOf(ui->table->item(row,columnTuningStep)->text());
    }

    if (!ui->table->isColumnHidden(columnCustomTuningStep) && ui->table->item(row,columnCustomTuningStep) != NULL) {
        currentMemory.progTs =(ui->table->item(row,columnCustomTuningStep) == NULL) ? 0 : int(ui->table->item(row,columnCustomTuningStep)->text().toUInt());
    }

    if (!ui->table->isColumnHidden(columnAttenuator) && ui->table->item(row,columnAttenuator) != NULL) {
        currentMemory.atten = (ui->table->item(row,columnAttenuator) == NULL) ? 0 : int(ui->table->item(row,columnAttenuator)->text().toUInt());
    }

    if (!ui->table->isColumnHidden(columnPreamplifier) && ui->table->item(row,columnPreamplifier) != NULL) {
        currentMemory.preamp = preamps.indexOf(ui->table->item(row,columnPreamplifier)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnAntenna) && ui->table->item(row,columnAntenna) != NULL) {
        currentMemory.antenna = antennas.indexOf(ui->table->item(row,columnAntenna)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnIPPlus) && ui->table->item(row,columnIPPlus) != NULL) {
        currentMemory.ipplus = ipplus.indexOf(ui->table->item(row,columnIPPlus)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnDSQL) && ui->table->item(row,columnDSQL) != NULL) {
        currentMemory.dsql = dsql.indexOf(ui->table->item(row,columnDSQL)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnDSQLB) && ui->table->item(row,columnDSQLB) != NULL) {
        currentMemory.dsqlB = dsql.indexOf(ui->table->item(row,columnDSQLB)->text().toUpper());
    }

    currentMemory.tone = (ui->table->item(row,columnTone) == NULL) ? "67.0" : ui->table->item(row,columnTone)->text();
    currentMemory.toneB = (ui->table->item(row,columnToneB) == NULL) ? "67.0" : ui->table->item(row,columnToneB)->text();

    currentMemory.tsql = (ui->table->item(row,columnTSQL) == NULL) ? "67.0" : ui->table->item(row,columnTSQL)->text();
    currentMemory.tsqlB = (ui->table->item(row,columnTSQLB) == NULL) ? "67.0" : ui->table->item(row,columnTSQLB)->text();

    currentMemory.dtcs = (ui->table->item(row,columnDTCS) == NULL) ? 23 : int(ui->table->item(row,columnDTCS)->text().toUInt());
    currentMemory.dtcsB = (ui->table->item(row,columnDTCSB) == NULL) ? 23 : int(ui->table->item(row,columnDTCSB)->text().toUInt());

    if (!ui->table->isColumnHidden(columnDTCSPolarity) && ui->table->item(row,columnDTCSPolarity) != NULL) {
        currentMemory.dtcsp = dtcsp.indexOf(ui->table->item(row,columnDTCSPolarity)->text().toUpper());
    }

    if (!ui->table->isColumnHidden(columnDTCSPolarityB) && ui->table->item(row,columnDTCSPolarityB) != NULL) {
        currentMemory.dtcspB = dtcsp.indexOf(ui->table->item(row,columnDTCSPolarityB)->text().toUpper());
    }

    currentMemory.dvsql = (ui->table->item(row,columnDVSquelch) == NULL) ? 0 : int(ui->table->item(row,columnDVSquelch)->text().toUInt());
    currentMemory.dvsqlB = (ui->table->item(row,columnDVSquelchB) == NULL) ? 0 : int(ui->table->item(row,columnDVSquelchB)->text().toUInt());

    currentMemory.duplexOffset.MHzDouble = (ui->table->item(row,columnOffset) == NULL) ? 0.0 : ui->table->item(row,columnOffset)->text().toDouble();
    currentMemory.duplexOffset.Hz=currentMemory.duplexOffset.MHzDouble*1000000.0;
    currentMemory.duplexOffset.VFO=selVFO_t::activeVFO;

    currentMemory.duplexOffsetB.MHzDouble = (ui->table->item(row,columnOffsetB) == NULL) ? 0.0 : ui->table->item(row,columnOffsetB)->text().toDouble();
    currentMemory.duplexOffsetB.Hz=currentMemory.duplexOffsetB.MHzDouble*1000000.0;
    currentMemory.duplexOffsetB.VFO=selVFO_t::activeVFO;


    memcpy(currentMemory.UR,((ui->table->item(row,columnUR) == NULL) ? "" : ui->table->item(row,columnUR)->text()).toStdString().c_str(),8);
    memcpy(currentMemory.URB,((ui->table->item(row,columnURB) == NULL) ? "" : ui->table->item(row,columnURB)->text()).toStdString().c_str(),8);

    memcpy(currentMemory.R1,((ui->table->item(row,columnR1) == NULL) ? "" : ui->table->item(row,columnR1)->text()).toStdString().c_str(),8);
    memcpy(currentMemory.R1B,((ui->table->item(row,columnR1B) == NULL) ? "" : ui->table->item(row,columnR1B)->text()).toStdString().c_str(),8);

    memcpy(currentMemory.R2,((ui->table->item(row,columnR2) == NULL) ? "" : ui->table->item(row,columnR2)->text()).toStdString().c_str(),8);
    memcpy(currentMemory.R2B,((ui->table->item(row,columnR2B) == NULL) ? "" : ui->table->item(row,columnR2B)->text()).toStdString().c_str(),8);

    memcpy(currentMemory.name,((ui->table->item(row,columnName) == NULL) ? "" : ui->table->item(row,columnName)->text()).toStdString().c_str(),16);


    if (ui->table->item(row,columnP25Sql) != NULL) {
        currentMemory.p25Sql = p25Sql.indexOf(ui->table->item(row,columnP25Sql)->text().toUpper());
    }
    currentMemory.p25Nac = (ui->table->item(row,columnP25Nac) == NULL) ? 0 : int(ui->table->item(row,columnP25Nac)->text().toUInt(nullptr,16));

    if (ui->table->item(row,columnDPmrSql) != NULL) {
        currentMemory.dPmrSql = dPmrSql.indexOf(ui->table->item(row,columnDPmrSql)->text().toUpper());
    }
    currentMemory.dPmrComid = (ui->table->item(row,columnDPmrComid) == NULL) ? 0 : int(ui->table->item(row,columnDPmrComid)->text().toUInt());
    currentMemory.dPmrCc = (ui->table->item(row,columnDPmrCc) == NULL) ? 0 : int(ui->table->item(row,columnDPmrCc)->text().toUInt());
    if (ui->table->item(row,columnDPmrSCRM) != NULL) {
        currentMemory.dPmrSCRM = dPmrSCRM.indexOf(ui->table->item(row,columnDPmrSCRM)->text().toUpper());
    }
    currentMemory.dPmrKey = (ui->table->item(row,columnDPmrKey) == NULL) ? 0 : int(ui->table->item(row,columnDPmrKey)->text().toUInt());

    if (ui->table->item(row,columnNxdnSql) != NULL) {
        currentMemory.nxdnSql = nxdnSql.indexOf(ui->table->item(row,columnNxdnSql)->text().toUpper());
    }
    currentMemory.nxdnRan = (ui->table->item(row,columnNxdnRan) == NULL) ? 0 : int(ui->table->item(row,columnNxdnRan)->text().toUInt());
    if (ui->table->item(row,columnNxdnEnc) != NULL) {
        currentMemory.nxdnEnc = nxdnEnc.indexOf(ui->table->item(row,columnNxdnEnc)->text().toUpper());
    }
    currentMemory.nxdnKey = (ui->table->item(row,columnNxdnKey) == NULL) ? 0 : int(ui->table->item(row,columnNxdnKey)->text().toUInt());

    if (ui->table->item(row,columnDcrSql) != NULL) {
        currentMemory.dcrSql = dcrSql.indexOf(ui->table->item(row,columnDcrSql)->text().toUpper());
    }
    currentMemory.dcrUc = (ui->table->item(row,columnDcrUc) == NULL) ? 0 : int(ui->table->item(row,columnDcrUc)->text().toUInt());
    if (ui->table->item(row,columnDcrEnc) != NULL) {
        currentMemory.dcrEnc = dcrEnc.indexOf(ui->table->item(row,columnDcrEnc)->text().toUpper());
    }
    currentMemory.dcrKey = (ui->table->item(row,columnDcrKey) == NULL) ? 0 : int(ui->table->item(row,columnDcrKey)->text().toUInt());

    // This may update any dependant columns so signals must be blocked.
    bool block=false;
    if (!ui->table->signalsBlocked()) {
        block=true;
        ui->table->blockSignals(true);
    }
    switch (col)
    {
    case columnVFO:
    case columnVFOB:
        if (!ui->table->isColumnHidden(columnVFOB) && ui->table->item(row,columnVFOB) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnVFOB),ui->table->item(row,columnVFO)->text());
        else if (!ui->table->isColumnHidden(columnVFO) && ui->table->item(row,columnVFO) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnVFO),ui->table->item(row,columnVFOB)->text());
        break;
    case columnFrequency:
    case columnFrequencyB:
        if (!ui->table->isColumnHidden(columnFrequencyB) && ui->table->item(row,columnFrequencyB) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnFrequencyB),ui->table->item(row,columnFrequency)->text());
        else if (!ui->table->isColumnHidden(columnFrequency) && ui->table->item(row,columnFrequency) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnFrequency),ui->table->item(row,columnFrequencyB)->text());
        break;
    case columnMode:
    case columnModeB:
    {
        if (!ui->table->isColumnHidden(columnModeB) && ui->table->item(row,columnModeB) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnModeB),ui->table->item(row,columnMode)->text());
        else if (!ui->table->isColumnHidden(columnMode) && ui->table->item(row,columnMode) == NULL)
            ui->table->model()->setData(ui->table->model()->index(row,columnMode),ui->table->item(row,columnModeB)->text());

        for (auto &m: rigCaps->modes){
            if (!ui->table->isColumnHidden(columnMode) && ui->table->item(row,columnMode) != NULL && ui->table->item(row,columnMode)->text()==m.name) {
                // This mode is the one we are interested in!
                configColumns(row,m,currentMemory.split);
                break;
            }
            if (!ui->table->isColumnHidden(columnModeB) && ui->table->item(row,columnModeB) != NULL && ui->table->item(row,columnModeB)->text()==m.name) {
                // This mode is the one we are interested in!
                configColumnsB(row,m);
                break;
            }
        }
        break;
    }
    case columnDuplex:
        ui->table->model()->setData(ui->table->model()->index(row,columnDuplexB),ui->table->item(row,columnDuplex)->text());
        break;
    case columnDuplexB:
        ui->table->model()->setData(ui->table->model()->index(row,columnDuplex),ui->table->item(row,columnDuplexB)->text());
        break;
    case columnOffset:
        ui->table->model()->setData(ui->table->model()->index(row,columnOffsetB),ui->table->item(row,columnOffset)->text());
        break;
    case columnOffsetB:
        ui->table->model()->setData(ui->table->model()->index(row,columnOffset),ui->table->item(row,columnOffsetB)->text());
        break;
    case columnTuningStep:
        if (ui->table->item(row,columnTuningStep) != NULL && ui->table->item(row,columnTuningStep)->text() == "Prog")
            enableCell(row,columnCustomTuningStep,true);
        else
            enableCell(row,columnCustomTuningStep,false);

        break;
    default:
        break;
    }

    if (block)
        ui->table->blockSignals(false);


    // Only write the memory if ALL required values are non-null
    bool write=true;
    for (int f=1; f<ui->table->columnCount();f++)
    {
        if (!ui->table->isColumnHidden(f) && ui->table->item(row,f) == NULL) {
            write=false;
            qInfo() << "Column << f << is nulL, cannot write.";
            //ui->table->model()->setData(ui->table->model()->index(row,f),QString(""));
            //ui->table->item(row,f)->setBackground(Qt::red);
        } else {
            //ui->table->item(row,f)->setBackground(Qt::NoBrush);
        }

    }
    // Quick sanity check as we MUST have at least one valid frequency
    if (currentMemory.frequency.Hz == 0 || (rigCaps->manufacturer == manufIcom && currentMemory.split && currentMemory.frequencyB.Hz == 0))
    {
        write=false;
        qInfo() << "Frequency is zero";
    }

    if (write) {
        //Sent command to write memory followed by slightly lower priority command to read.
        queue->add(priorityImmediate,queueItem((currentMemory.sat?funcSatelliteMemory:writeCommand),QVariant::fromValue<memoryType>(currentMemory)));
        if (rigCaps->commands.contains(funcMemoryTag) || rigCaps->commands.contains(funcMemoryTagB))
        {
            memoryTagType tag;
            tag.num=currentMemory.channel;
            tag.name = QString(currentMemory.name);
            tag.en=1;
            queue->add(priorityHighest,queueItem(rigCaps->commands.contains(funcMemoryTagB)?funcMemoryTagB:funcMemoryTag,QVariant::fromValue<memoryTagType>(tag)));
            queue->add(priorityHigh,queueItem(rigCaps->commands.contains(funcMemoryTagB)?funcMemoryTagB:funcMemoryTag,QVariant::fromValue<uint>(currentMemory.channel)));
        }
        if (rigCaps->commands.contains(funcSplitMemory))
        {
            // Yaesu memory split command.
            memorySplitType s;
            s.num=currentMemory.channel;
            s.en = (currentMemory.frequency.Hz != currentMemory.frequencyB.Hz && currentMemory.frequencyB.Hz != 0)?true:false;
            s.freq = currentMemory.frequencyB.Hz;
            qInfo() << "Adding split command for:" <<s.num<<" en:" << s.en << " freq:" << s.freq;
            queue->add(priorityHighest,queueItem(funcSplitMemory,QVariant::fromValue<memorySplitType>(s)));
            queue->add(priorityHigh,queueItem(funcSplitMemory,QVariant::fromValue<uint>(currentMemory.channel)));

        }
        queue->add(priorityHigh,queueItem(funcMemoryContents,QVariant::fromValue<uint>((currentMemory.group<<16) | (currentMemory.channel & 0xffff))));
        qDebug() << "Sending memory, group:" << currentMemory.group << "channel" << currentMemory.channel;
        // Set number to not be editable once written. Not sure why but this crashes?
        //ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() & (~Qt::ItemIsEditable));
    }
}

/*
 * This function is called whenever the group changes (and on startup)
 * It configures the table with the required columns and requests the
 * first memory from the radio.
 */
void memories::on_group_currentIndexChanged(int index)
{
    Q_UNUSED(index)

    timeoutTimer.start(MEMORY_TIMEOUT);

    visibleColumns=1;

    // Special case for group 100 on the IC705!
    if (ui->group->currentData().toInt() ==  MEMORY_SATGROUP)
        groupMemories=rigCaps->satMemories;
    else if (ui->group->currentData().toInt() == MEMORY_SHORTGROUP)
        groupMemories=3;
    else
        groupMemories=rigCaps->memories;

    progress->setRange(rigCaps->memStart,groupMemories);

    //ui->loadingMemories->setVisible(true);
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

    if (ui->group->currentData().toInt() == MEMORY_SATGROUP) {
        on_modeButton_clicked(true);
        queue->add(priorityImmediate,queueItem(funcSatelliteMode,QVariant::fromValue(true),false,0));
        parser = rigCaps->satParser;
    } else {
        // If the rig has memory groups, select it now.
        queue->add(priorityImmediate,queueItem(funcSatelliteMode,QVariant::fromValue(false),false,0)); // Make sure we are in not in satellite mode.
        queue->add(priorityImmediate,queueItem(funcMemoryGroup,QVariant::fromValue<uchar>(ui->group->currentData().toInt())));
        parser = rigCaps->memParser;
    }

    for (auto &parse: parser) {
        switch (parse.spec)
        {
        case 'a':
            ui->group->show();
            disableCommands = true;
            break;
        case 'b':
            if (numEditor != Q_NULLPTR)
                delete numEditor;
            numEditor = new tableEditor("999",ui->table);
            ui->table->setItemDelegateForColumn(columnNum, numEditor);
            ui->table->showColumn(columnNum);
            visibleColumns++;
            break;
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
        case 'C':
            if (skipList != Q_NULLPTR)
                delete skipList;
            skipList = new tableCombobox(createModel(skipModel, skip),false,ui->table);
            ui->table->setItemDelegateForColumn(columnSkip, skipList);
            ui->table->showColumn(columnSkip);
            visibleColumns++;
            // Fallthrough to add Scanlist column as well.
            scan.clear();
            scan << "OFF" << "*1" << "*2" << "*3" << "*4" << "*5" << "*6" << "*7" << "*8" << "*9";
        case 'c':
            if (scanList != Q_NULLPTR)
                delete scanList;
            scanList = new tableCombobox(createModel(scanModel, scan),false,ui->table);
            ui->table->setItemDelegateForColumn(columnScan, scanList);

            ui->table->showColumn(columnScan);
            visibleColumns++;
            break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
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

            visibleColumns++;
            visibleColumns++;
            break;
        case 'D': // Duplex only added for IC-R8600 (most rigs use j for duplex+tone)
            if (duplexList != Q_NULLPTR)
                delete duplexList;
            duplexList = new tableCombobox(createModel(duplexModel, duplexModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDuplex, duplexList);
            ui->table->showColumn(columnDuplex);
            visibleColumns++;
            break;
        case 'e':
            if (vfoList != Q_NULLPTR)
                delete vfoList;
            vfoList = new tableCombobox(createModel(vfoModel, vfos),false,ui->table);
            ui->table->setItemDelegateForColumn(columnVFO, vfoList);

            ui->table->showColumn(columnVFO);
            visibleColumns++;
            break;
        case 'E':
            if (vfoListB != Q_NULLPTR)
                delete vfoListB;
            vfoListB = new tableCombobox(createModel(vfoModelB, vfos),false,ui->table);
            ui->table->setItemDelegateForColumn(columnVFOB, vfoListB);

            ui->table->showColumn(columnVFOB);
            visibleColumns++;
            break;
        case 'f':
            if (freqEditor != Q_NULLPTR)
                delete freqEditor;
            freqEditor = new tableEditor("00000.00000",ui->table);
            ui->table->setItemDelegateForColumn(columnFrequency, freqEditor);

            ui->table->showColumn(columnFrequency);
            visibleColumns++;
            break;
        case 'F':
            if (freqEditorB != Q_NULLPTR)
                delete freqEditorB;
            freqEditorB = new tableEditor("00000.00000",ui->table);
            ui->table->setItemDelegateForColumn(columnFrequencyB, freqEditorB);

            ui->table->showColumn(columnFrequencyB);
            visibleColumns++;
            break;
        case 'g':
            if (modesList != Q_NULLPTR)
                delete modesList;
            modesList = new tableCombobox(createModel(modesModel, modes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnMode, modesList);

            ui->table->showColumn(columnMode);
            visibleColumns++;
            break;
        case 'G':
            if (modesListB != Q_NULLPTR)
                delete modesListB;
            modesListB = new tableCombobox(createModel(modesModelB, modes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnModeB, modesListB);

            ui->table->showColumn(columnModeB);
            visibleColumns++;
            break;
        case 'h':
            if (filterList != Q_NULLPTR)
                delete filterList;
            filterList = new tableCombobox(createModel(filterModel, filters),false,ui->table);
            ui->table->setItemDelegateForColumn(columnFilter, filterList);

            ui->table->showColumn(columnFilter);
            visibleColumns++;
            break;
        case 'H':
            if (filterListB != Q_NULLPTR)
                delete filterListB;
            filterListB = new tableCombobox(createModel(filterModelB, filters),false,ui->table);
            ui->table->setItemDelegateForColumn(columnFilterB, filterListB);

            ui->table->showColumn(columnFilterB);
            visibleColumns++;
            break;
        case 'i':
            if (dataList != Q_NULLPTR)
                delete dataList;
            dataList = new tableCombobox(createModel(dataModel, dataModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnData, dataList);

            ui->table->showColumn(columnData);
            visibleColumns++;
            break;
        case 'I':
            if (dataListB != Q_NULLPTR)
                delete dataListB;
            dataListB = new tableCombobox(createModel(dataModelB, dataModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDataB, dataListB);

            ui->table->showColumn(columnDataB);
            visibleColumns++;
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
            visibleColumns++;
            visibleColumns++;
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
            visibleColumns++;
            visibleColumns++;
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
            visibleColumns++;
            visibleColumns++;
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
            visibleColumns++;
            visibleColumns++;
            break;
        case 'l':
            if (toneModesList != Q_NULLPTR)
                delete toneModesList;
            toneModesList = new tableCombobox(createModel(toneModesModel, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneMode, toneModesList);

            ui->table->showColumn(columnToneMode);
            visibleColumns++;
            break;
        case 'L':
            if (toneModesListB != Q_NULLPTR)
                delete toneModesListB;
            toneModesListB = new tableCombobox(createModel(toneModesModelB, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneModeB, toneModesListB);

            ui->table->showColumn(columnToneModeB);
            visibleColumns++;
            break;
        case 'm':
            if (dsqlList != Q_NULLPTR)
                delete dsqlList;
            dsqlList = new tableCombobox(createModel(dsqlModel, dsql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDSQL, dsqlList);

            ui->table->showColumn(columnDSQL);
            visibleColumns++;
            break;
        case 'M':
            if (dsqlListB != Q_NULLPTR)
                delete dsqlListB;
            dsqlListB = new tableCombobox(createModel(dsqlModelB, dsql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDSQLB, dsqlListB);

            ui->table->showColumn(columnDSQLB);
            visibleColumns++;
            break;
        case 'n':
            if (tonesList != Q_NULLPTR)
                delete tonesList;
            tonesList = new tableCombobox(createModel(tonesModel, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnTone, tonesList);

            ui->table->showColumn(columnTone);
            visibleColumns++;
            break;
        case 'N':
            if (tonesListB != Q_NULLPTR)
                delete tonesListB;
            tonesListB = new tableCombobox(createModel(tonesModelB, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneB, tonesListB);

            ui->table->showColumn(columnToneB);
            visibleColumns++;
            break;
        case 'o':
            if (tsqlList != Q_NULLPTR)
                delete tsqlList;
            tsqlList = new tableCombobox(createModel(tsqlModel, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnTSQL, tsqlList);

            ui->table->showColumn(columnTSQL);
            visibleColumns++;
            break;
        case 'O':
            if (tsqlListB != Q_NULLPTR)
                delete tsqlListB;
            tsqlListB = new tableCombobox(createModel(tsqlModelB, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnTSQLB, tsqlListB);

            ui->table->showColumn(columnTSQLB);
            visibleColumns++;
            break;
        case 'p':
            if (dtcspList != Q_NULLPTR)
                delete dtcspList;
            dtcspList = new tableCombobox(createModel(dtcspModel, dtcsp),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCSPolarity, dtcspList);

            ui->table->showColumn(columnDTCSPolarity);
            visibleColumns++;
            break;
        case 'P':
            if (dtcspListB != Q_NULLPTR)
                delete dtcspListB;
            dtcspListB = new tableCombobox(createModel(dtcspModelB, dtcsp),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCSPolarityB, dtcspListB);

            ui->table->showColumn(columnDTCSPolarityB);
            visibleColumns++;
            break;
        case 'q':
            if (dtcsList != Q_NULLPTR)
                delete dtcsList;
            dtcsList = new tableCombobox(createModel(dtcsModel, dtcs),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCS, dtcsList);

            ui->table->showColumn(columnDTCS);
            visibleColumns++;
            break;
        case 'Q':
            if (dtcsListB != Q_NULLPTR)
                delete dtcsListB;
            dtcsListB = new tableCombobox(createModel(dtcsModel, dtcs),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCSB, dtcsList);

            ui->table->showColumn(columnDTCSB);
            visibleColumns++;
            break;
        case 'r':
            if (dvsqlList != Q_NULLPTR)
                delete dvsqlList;
            dvsqlList = new tableCombobox(createModel(dvsqlModel, dvsql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDVSquelch, dvsqlList);
            ui->table->showColumn(columnDVSquelch);
            visibleColumns++;
            break;
        case 'R':
            if (dvsqlListB != Q_NULLPTR)
                delete dvsqlListB;
            dvsqlListB = new tableCombobox(createModel(dvsqlModelB, dvsql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDVSquelchB, dvsqlListB);
            ui->table->showColumn(columnDVSquelchB);
            visibleColumns++;
            break;
        case 's':
            if (offsetEditor != Q_NULLPTR)
                delete offsetEditor;
            offsetEditor = new tableEditor("00.000000",ui->table);
            ui->table->setItemDelegateForColumn(columnOffset, offsetEditor);

            ui->table->showColumn(columnOffset);
            visibleColumns++;
            break;
        case 'S':
            if (offsetEditorB != Q_NULLPTR)
                delete offsetEditorB;
            offsetEditorB = new tableEditor("00.000000",ui->table);
            ui->table->setItemDelegateForColumn(columnOffsetB, offsetEditorB);

            ui->table->showColumn(columnOffsetB);
            visibleColumns++;
            break;
        case 't':
            if (urEditor != Q_NULLPTR)
                delete urEditor;
            urEditor = new tableEditor(">xxxxxxxx;_",ui->table);
            ui->table->setItemDelegateForColumn(columnUR, urEditor);

            ui->table->showColumn(columnUR);
            visibleColumns++;
            break;
        case 'T':
            if (urEditorB != Q_NULLPTR)
                delete urEditorB;
            urEditorB = new tableEditor(">xxxxxxxx;_",ui->table);
            ui->table->setItemDelegateForColumn(columnURB, urEditorB);

            ui->table->showColumn(columnURB);
            visibleColumns++;
            break;
        case 'u':
            if (r1Editor != Q_NULLPTR)
                delete r1Editor;
            r1Editor = new tableEditor(">xxxxxxxx;_",ui->table);
            ui->table->setItemDelegateForColumn(columnR1, r1Editor);

            ui->table->showColumn(columnR1);
            visibleColumns++;
            break;
        case 'U':
            if (r1EditorB != Q_NULLPTR)
                delete r1EditorB;
            r1EditorB = new tableEditor(">xxxxxxxx;_",ui->table);
            ui->table->setItemDelegateForColumn(columnR1B, r1EditorB);

            ui->table->showColumn(columnR1B);
            visibleColumns++;
            break;
        case 'v':
            if (r2Editor != Q_NULLPTR)
                delete r2Editor;
            r2Editor = new tableEditor(">xxxxxxxx;_",ui->table);
            ui->table->setItemDelegateForColumn(columnR2, r2Editor);

            ui->table->showColumn(columnR2);
            visibleColumns++;
            break;
        case 'V':
            if (r2EditorB != Q_NULLPTR)
                delete r2EditorB;
            r2EditorB = new tableEditor(">xxxxxxxx;_",ui->table);
            ui->table->setItemDelegateForColumn(columnR2B, r2EditorB);

            ui->table->showColumn(columnR2B);
            visibleColumns++;
            break;
        case 'W':
            if (clarEditor != Q_NULLPTR)
            {
                delete clarEditor;
            }
            clarEditor = new tableEditor("x00000",ui->table);
            ui->table->setItemDelegateForColumn(columnClarOffset, clarEditor);
            ui->table->showColumn(columnClarOffset);
            visibleColumns++;
            break;
        case 'X':
            if (clarRXList == Q_NULLPTR)
            {
                clarRXList = new tableCombobox(createModel(clarRXModel, clar),false,ui->table);
            }
            ui->table->setItemDelegateForColumn(columnRXClar, clarRXList);
            ui->table->showColumn(columnRXClar);
            visibleColumns++;
            break;
        case 'Y':
            if (clarTXList == Q_NULLPTR)
            {
                clarTXList = new tableCombobox(createModel(clarTXModel, clar),false,ui->table);
            }
            ui->table->setItemDelegateForColumn(columnTXClar, clarTXList);
            ui->table->showColumn(columnTXClar);
            visibleColumns++;
            break;
        case 'w':
            if (tuningStepsList != Q_NULLPTR)
                delete tuningStepsList;
            tuningStepsList = new tableCombobox(createModel(tuningStepsModel, tuningSteps),false,ui->table);
            ui->table->setItemDelegateForColumn(columnTuningStep, tuningStepsList);
            ui->table->showColumn(columnTuningStep);
            visibleColumns++;

            if (tuningStepEditor != Q_NULLPTR)
                delete tuningStepEditor;
            tuningStepEditor = new tableEditor("0000",ui->table);
            ui->table->setItemDelegateForColumn(columnCustomTuningStep, tuningStepEditor);
            ui->table->showColumn(columnCustomTuningStep);
            visibleColumns++;

            break;
        case 'x':
            if (attenuatorsList != Q_NULLPTR)
                delete attenuatorsList;
            attenuatorsList = new tableCombobox(createModel(attenuatorsModel, attenuators),false,ui->table);
            ui->table->setItemDelegateForColumn(columnAttenuator, attenuatorsList);
            ui->table->showColumn(columnAttenuator);
            visibleColumns++;

            if (preampsList != Q_NULLPTR)
                delete preampsList;
            preampsList = new tableCombobox(createModel(preampsModel, preamps),false,ui->table);
            ui->table->setItemDelegateForColumn(columnPreamplifier, preampsList);
            ui->table->showColumn(columnPreamplifier);
            visibleColumns++;
            break;
        case 'y':
            if (antennasList != Q_NULLPTR)
                delete antennasList;
            antennasList = new tableCombobox(createModel(antennasModel, antennas),false,ui->table);
            ui->table->setItemDelegateForColumn(columnAntenna, antennasList);
            ui->table->showColumn(columnAntenna);
            visibleColumns++;
            break;
        case '+':
            if (ipplusList != Q_NULLPTR)
                delete ipplusList;
            ipplusList = new tableCombobox(createModel(ipplusModel, ipplus),false,ui->table);
            ui->table->setItemDelegateForColumn(columnIPPlus, ipplusList);
            ui->table->showColumn(columnIPPlus);
            visibleColumns++;
            break;
        case 'z':
            if (nameEditor != Q_NULLPTR)
                delete nameEditor;
            nameEditor = new tableEditor(QString("%0;_").arg("",parse.len,'x'),ui->table);
            ui->table->setItemDelegateForColumn(columnName, nameEditor);

            ui->table->showColumn(columnName);
            visibleColumns++;
            break;
        case 'Z':
            extendedView = true;
            if (toneModesList != Q_NULLPTR)
                delete toneModesList;
            toneModesList = new tableCombobox(createModel(toneModesModel, toneModes),false,ui->table);
            ui->table->setItemDelegateForColumn(columnToneMode, toneModesList);

            ui->table->showColumn(columnToneMode);
            //visibleColumns++;

            if (tsqlList != Q_NULLPTR)
                delete tsqlList;
            tsqlList = new tableCombobox(createModel(tsqlModel, tones),false,ui->table);
            ui->table->setItemDelegateForColumn(columnTSQL, tsqlList);
            ui->table->showColumn(columnTSQL);
            //visibleColumns++;

            if (dtcsList != Q_NULLPTR)
                delete dtcsList;
            dtcsList = new tableCombobox(createModel(dtcsModel, dtcs),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCS, dtcsList);
            ui->table->showColumn(columnDTCS);
            //visibleColumns++;

            if (dtcspList != Q_NULLPTR)
                delete dtcspList;
            dtcspList = new tableCombobox(createModel(dtcspModel, dtcsp),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDTCSPolarity, dtcspList);
            ui->table->showColumn(columnDTCSPolarity);
            //visibleColumns++;

            if (dsqlList != Q_NULLPTR)
                delete dsqlList;
            dsqlList = new tableCombobox(createModel(dsqlModel, dsql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDSQL, dsqlList);
            ui->table->showColumn(columnDSQL);
            //visibleColumns++;

            if (dvsqlList != Q_NULLPTR)
                delete dvsqlList;
            dvsqlList = new tableCombobox(createModel(dvsqlModel, dvsql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDVSquelch, dvsqlList);
            ui->table->showColumn(columnDVSquelch);
            //visibleColumns++;

            /* P25 */
            if (p25SqlList != Q_NULLPTR)
                delete p25SqlList;
            p25SqlList = new tableCombobox(createModel(p25SqlModel, p25Sql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnP25Sql, p25SqlList);
            ui->table->showColumn(columnP25Sql);
            //visibleColumns++;

            if (p25NacEditor != Q_NULLPTR)
                delete p25NacEditor;
            p25NacEditor = new tableEditor("HHH",ui->table);
            ui->table->setItemDelegateForColumn(columnP25Nac, p25NacEditor);
            ui->table->showColumn(columnP25Nac);
            //visibleColumns++;

            /* dPMR */
            if (dPmrSqlList != Q_NULLPTR)
                delete dPmrSqlList;
            dPmrSqlList = new tableCombobox(createModel(dPmrSqlModel, dPmrSql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDPmrSql, dPmrSqlList);
            ui->table->showColumn(columnDPmrSql);
            //visibleColumns++;

            if (dPmrComIdEditor != Q_NULLPTR)
                delete dPmrComIdEditor;
            dPmrComIdEditor = new tableEditor("999",ui->table);
            ui->table->setItemDelegateForColumn(columnDPmrComid, dPmrComIdEditor);
            ui->table->showColumn(columnDPmrComid);
            //visibleColumns++;

            if (dPmrCcEditor != Q_NULLPTR)
                delete dPmrCcEditor;
            dPmrCcEditor = new tableEditor("99",ui->table);
            ui->table->setItemDelegateForColumn(columnDPmrCc, dPmrCcEditor);
            ui->table->showColumn(columnDPmrCc);
            //visibleColumns++;

            if (dPmrSCRMList != Q_NULLPTR)
                delete dPmrSCRMList;
            dPmrSCRMList = new tableCombobox(createModel(dPmrSCRMModel, dPmrSCRM),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDPmrSCRM, dPmrSCRMList);
            ui->table->showColumn(columnDPmrSCRM);
            //visibleColumns++;

            if (dPmrKeyEditor != Q_NULLPTR)
                delete dPmrKeyEditor;
            dPmrKeyEditor = new tableEditor("99999",ui->table);
            ui->table->setItemDelegateForColumn(columnDPmrKey, dPmrKeyEditor);
            ui->table->showColumn(columnDPmrKey);
            //visibleColumns++;


            /* NXDN */
            if (nxdnSqlList != Q_NULLPTR)
                delete nxdnSqlList;
            nxdnSqlList = new tableCombobox(createModel(nxdnSqlModel, nxdnSql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnNxdnSql, nxdnSqlList);
            ui->table->showColumn(columnNxdnSql);
            //visibleColumns++;

            if (nxdnRanEditor != Q_NULLPTR)
                delete nxdnRanEditor;
            nxdnRanEditor = new tableEditor("99",ui->table);
            ui->table->setItemDelegateForColumn(columnNxdnRan, nxdnRanEditor);
            ui->table->showColumn(columnNxdnRan);
            //visibleColumns++;

            if (nxdnEncList != Q_NULLPTR)
                delete nxdnEncList;
            nxdnEncList = new tableCombobox(createModel(nxdnEncModel, nxdnEnc),false,ui->table);
            ui->table->setItemDelegateForColumn(columnNxdnEnc, nxdnEncList);
            ui->table->showColumn(columnNxdnEnc);
            //visibleColumns++;

            if (nxdnKeyEditor != Q_NULLPTR)
                delete nxdnKeyEditor;
            nxdnKeyEditor = new tableEditor("99999",ui->table);
            ui->table->setItemDelegateForColumn(columnNxdnKey, nxdnKeyEditor);
            ui->table->showColumn(columnNxdnKey);
            //visibleColumns++;

            /* DCR */
            if (dcrSqlList != Q_NULLPTR)
                delete dcrSqlList;
            dcrSqlList = new tableCombobox(createModel(dcrSqlModel, dcrSql),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDcrSql, dcrSqlList);
            ui->table->showColumn(columnDcrSql);
            //visibleColumns++;

            if (dcrUcEditor != Q_NULLPTR)
                delete dcrUcEditor;
            dcrUcEditor = new tableEditor("999",ui->table);
            ui->table->setItemDelegateForColumn(columnDcrUc, dcrUcEditor);
            ui->table->showColumn(columnDcrUc);
            //visibleColumns++;

            if (dcrEncList != Q_NULLPTR)
                delete dcrEncList;
            dcrEncList = new tableCombobox(createModel(dcrEncModel, dcrEnc),false,ui->table);
            ui->table->setItemDelegateForColumn(columnDcrEnc, dcrEncList);
            ui->table->showColumn(columnDcrEnc);
            //visibleColumns++;

            if (dcrKeyEditor != Q_NULLPTR)
                delete dcrKeyEditor;
            dcrKeyEditor = new tableEditor("99999",ui->table);
            ui->table->setItemDelegateForColumn(columnDcrKey, dcrKeyEditor);
            ui->table->showColumn(columnDcrKey);
            //visibleColumns++;

            break;
        default:
            break;
        }
    }
    // Yaesu description is separate!
    if ((rigCaps->commands.contains(funcMemoryTag) || rigCaps->commands.contains(funcMemoryTagB)) && ui->table->isColumnHidden(columnName))
    {
        if (nameEditor != Q_NULLPTR)
            delete nameEditor;
        nameEditor = new tableEditor(QString("%0;_").arg("",12,'x'),ui->table);
        ui->table->setItemDelegateForColumn(columnName, nameEditor);

        ui->table->showColumn(columnName);
        visibleColumns++;
    }

    if (rigCaps->commands.contains(funcSplitMemory) && ui->table->isColumnHidden(columnFrequencyB))
    {
        if (freqEditorB != Q_NULLPTR)
            delete freqEditorB;
        freqEditorB = new tableEditor("00000.00000",ui->table);
        ui->table->setItemDelegateForColumn(columnFrequencyB, freqEditorB);

        ui->table->showColumn(columnFrequencyB);
        visibleColumns++;
    }

    if (visibleColumns > 15 || extendedView) {
        ui->table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    }
    else
    {
        ui->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }

    if (ui->group->currentData().toInt() == MEMORY_SATGROUP) {

        lastMemoryRequested = rigCaps->memStart;
        if (slowLoad) {
            QTimer::singleShot(MEMORY_SLOWLOAD, this, [this]{
                queue->add(priorityImmediate,queueItem(funcSatelliteMemory,QVariant::fromValue<ushort>(lastMemoryRequested & 0xffff)));
            });
        } else {
            queue->add(priorityImmediate,queueItem(funcSatelliteMemory,QVariant::fromValue<ushort>(lastMemoryRequested & 0xffff)));
        }
    } else {
        lastMemoryRequested = quint32((ui->group->currentData().toInt())<<16) | (rigCaps->memStart & 0xffff);
        if (slowLoad) {
            QTimer::singleShot(MEMORY_SLOWLOAD, this, [this]{
                queue->add(priorityImmediate,queueItem(funcMemoryContents,QVariant::fromValue<uint>(lastMemoryRequested)));
            });
        } else {
            // Is the current group attached to a particular band?
            for (auto &band: rigCaps->bands)
            {
                if (band.region == "" || band.region == region) {
                    if (ui->group->isVisible() && band.memGroup == ui->group->currentData().toInt())
                    {
                        bandStackType bs;
                        bs.band = band.bsr;
                        bs.reg = 1;
                        queue->add(priorityImmediate,queueItem(funcBandStackReg, QVariant::fromValue<bandStackType>(bs),false,uchar(0)));
                        break;
                    }
                }
            }
            queue->add(priorityImmediate,queueItem(funcMemoryContents,QVariant::fromValue<uint>(lastMemoryRequested)));
        }
    }
    if (rigCaps->commands.contains(funcMemoryTag) || rigCaps->commands.contains(funcMemoryTagB))
    {
        // Yaesu specific as tag (name) is stored seperately.
        if (slowLoad) {
            QTimer::singleShot(MEMORY_SLOWLOAD, this, [this]{
                queue->add(priorityImmediate,queueItem(rigCaps->commands.contains(funcMemoryTagB)?funcMemoryTagB:funcMemoryTag,QVariant::fromValue<uint>(lastMemoryRequested)));
            });
        } else {
            queue->add(priorityImmediate,queueItem(rigCaps->commands.contains(funcMemoryTagB)?funcMemoryTagB:funcMemoryTag,QVariant::fromValue<uint>(lastMemoryRequested)));
        }
    }

    if (rigCaps->commands.contains(funcSplitMemory))
    {
        // Yaesu specific as tag (name) is stored seperately.
        if (slowLoad) {
            QTimer::singleShot(MEMORY_SLOWLOAD, this, [this]{
                queue->add(priorityImmediate,queueItem(funcSplitMemory,QVariant::fromValue<uint>(lastMemoryRequested)));
            });
        } else {
            queue->add(priorityImmediate,queueItem(funcSplitMemory,QVariant::fromValue<uint>(lastMemoryRequested)));
        }
    }


}

void memories::on_modeButton_clicked(bool on)
{
    emit memoryMode(on);

    static bool checked=false;
    if (!on && checked) {
        qInfo() << "Selecting VFO Mode";
        // The IC9700 may be in satellite mode, so disable that first.
        if (rigCaps->commands.contains(funcSatelliteMode))
           queue->add(priorityImmediate,queueItem(funcSatelliteMode,QVariant::fromValue(false),false,0)); // Make sure we are in not in satellite mode.
        ui->modeButton->setText("Select Memory Mode");
        if (disableCommands) {
            for( const commandList &c: activeCommands) {
                queue->add(c.prio,c.func,true,c.receiver);
            }
            activeCommands.clear();
        }
        for (uchar i = 0;i < rigCaps->numReceiver;i++) {
            queue->add(priorityImmediate,funcVFOModeSelect,false,i);
        }
        checked=false;
    } else if (on && !checked) {
        qInfo() << "Selecting Memory Mode";
        ui->modeButton->setText("Select VFO Mode");
        for (uchar i = 0;i < rigCaps->numReceiver;i++) {
            queue->add(priorityImmediate,funcMemoryMode,false,i);
        }
        if (disableCommands) {
            activeCommands.clear();
            for (int i=0;i<rigCaps->numReceiver;i++) {
                for (const funcs &func:disabledCommands)
                {
                    queuePriority prio = queue->del(func,i);
                    if (prio != priorityNone) {
                        activeCommands.append(commandList(prio,func,i));
                    }
                }
            }
        }
        checked=true;
    }
    ui->modeButton->setChecked(checked);
}

void memories::receiveMemorySplit(memorySplitType s)
{
    // Now process the incoming memory name
    qDebug(logRig()) << "Memories: received memory split id:" << s.num << " en:" << s.en << " freq:" << s.freq;

    for (int n = 0; n<ui->table->rowCount();n++)
    {
        if (ui->table->item(n,columnNum) != NULL && ui->table->item(n,columnNum)->text().toInt() == s.num)
        {

            ui->table->blockSignals(true);
            if (s.freq > 0)
                ui->table->model()->setData(ui->table->model()->index(n,columnFrequencyB),QString::number(double(s.freq/1000000.0),'f',5));
            ui->table->blockSignals(false);
            break;
        }
    }

}

void memories::receiveMemoryName(memoryTagType tag)
{
    // We should already have the memory contents, so just fill in the name.
    qDebug(logRig()) << "Memories: received memory tag id:" << tag.num << " en:" << tag.en << " name:" << tag.name;

    // Now process the incoming memory name
    for (int n = 0; n<ui->table->rowCount();n++)
    {
        if (ui->table->item(n,columnNum) != NULL && ui->table->item(n,columnNum)->text().toInt() == tag.num)
        {
            if (checkASCII(tag.name)) {
                ui->table->blockSignals(true);
                ui->table->model()->setData(ui->table->model()->index(n,columnName),tag.name);
                ui->table->blockSignals(false);
            }
            break;
        }
    }

}

void memories::receiveMemory(memoryType mem)
{
    qDebug() << QString("Loading Memory %0/%1 (this may take a while!)").arg(lastMemoryRequested&0xffff,3,10,QLatin1Char('0')).arg(rigCaps->memories,3,10,QLatin1Char('0'));
    progress->setValue(lastMemoryRequested & 0xffff);
    // First, do we need to request the next memory?
    if ((lastMemoryRequested & 0xffff) < groupMemories && startup)
    {
        lastMemoryRequested++;
        if (mem.sat) {
            queue->add(priorityImmediate,queueItem(funcSatelliteMemory,QVariant::fromValue<ushort>(lastMemoryRequested & 0xffff)));
        } else {
            queue->add(priorityImmediate,queueItem(funcMemoryContents,QVariant::fromValue<uint>(lastMemoryRequested)));

            if (rigCaps->commands.contains(funcMemoryTag))
            {
                queue->add(priorityImmediate,queueItem(funcMemoryTag,QVariant::fromValue<uint>(lastMemoryRequested)));
            }
            if (rigCaps->commands.contains(funcMemoryTagB))
            {
                queue->add(priorityImmediate,queueItem(funcMemoryTagB,QVariant::fromValue<uint>(lastMemoryRequested)));
            }
            if (rigCaps->commands.contains(funcSplitMemory))
            {
                queue->add(priorityImmediate,queueItem(funcSplitMemory,QVariant::fromValue<uint>(lastMemoryRequested)));
            }

        }
        timeoutTimer.start(MEMORY_TIMEOUT);
    }
    else if (mem.channel == groupMemories)
    {
        timeoutTimer.stop();
        ui->group->setEnabled(true);
        //ui->loadingMemories->setVisible(false);
        if (!ui->disableEditing->isChecked())
        {
            ui->table->setEditTriggers(QAbstractItemView::DoubleClicked);
        }
        startup=false;
    }

    timeoutCount=0; // We have received a memory, so set the timeout to zero.

    int validData=1; // We have 1 more row than will ever be received.

    // Now process the incoming memory
    int row=-1;

    for (int n = 0; n<ui->table->rowCount();n++)
    {
        if (ui->table->item(n,columnNum) != NULL && ui->table->item(n,columnNum)->text().toInt() == mem.channel &&
                    (rigCaps->memGroups < 2 || mem.sat || mem.group == ui->group->currentData().toInt()))
        {
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
            ui->table->setCellWidget(row,columnRecall,recall);
            connect(recall, &QPushButton::clicked, this, [=]() {                
                recallMem(mem.channel);
            });

        }


        ui->table->model()->setData(ui->table->model()->index(row,columnNum),QString::number(mem.channel & 0xffff).rightJustified(3,'0'));
        ui->table->item(row,columnNum)->setFlags(ui->table->item(row,columnNum)->flags() & (~Qt::ItemIsEditable));
        ui->table->item(row,columnNum)->setBackground(Qt::transparent);
        validData++;

        validData = validData + updateRow(row,mem,false);

        if (retries > 2)
        {
            retries=0;
            return;
        }

        if (validData < visibleColumns) {
            qInfo(logRig()) << "Memory" << mem.channel << "Received valid data for" << validData << "columns, " << "expected" << visibleColumns << "requesting again";
            if (mem.sat) {
                queue->add(priorityImmediate,queueItem(funcSatelliteMemory,QVariant::fromValue<ushort>(mem.channel & 0xffff)));
            } else {
                queue->add(priorityImmediate,queueItem(funcMemoryContents,QVariant::fromValue<uint>((mem.group<<16) | (mem.channel & 0xffff))));
            }
            retries++;
        }
    }
    else if (row != -1)
    {
        // Check if we already have this memory as it might have failed to write?
        //ui->loadingMemories->setStyleSheet("QLabel {color: #ff0000}");
        ui->table->item(row,columnNum)->setBackground(Qt::red);
    }

}

int  memories::updateEntry(QStringList& combo, int row, columns column, quint8 data)
{
    int ret=1;
    if (!ui->table->isColumnHidden(column) && combo.size() > data)
    {
        ui->table->model()->setData(ui->table->model()->index(row,column),combo[data]);
    }
    else if (!ui->table->isColumnHidden(column))
    {
        qInfo() << "Column" << ui->table->horizontalHeaderItem(column)->text() << "Invalid data:" << data << "setting to default";
        ui->table->model()->setData(ui->table->model()->index(row,column),combo[0]);
        ret=1;
    } else {
        ret=0;
    }
    return ret;
}

int  memories::updateEntry(QStringList& combo, int row, columns column, QString data)
{
    int ret=1;
    if (!ui->table->isColumnHidden(column) && combo.contains(data))
    {
        ui->table->model()->setData(ui->table->model()->index(row,column),data);
    }
    else if (!ui->table->isColumnHidden(column))
    {
        qInfo() << "Column" << ui->table->horizontalHeaderItem(column)->text() << "Invalid data:" << data;
        ret=0;
    } else {
        ret=0;
    }
    return ret;
}

int  memories::updateEntry(int row, columns column, QString data)
{
    int ret=0;
    if (!ui->table->isColumnHidden(column))
    {
        ui->table->model()->setData(ui->table->model()->index(row,column),data);
        ret=1;
    }
    return ret;
}


bool memories::checkASCII(QString str)
{
    static QRegularExpression exp = QRegularExpression(QStringLiteral("[^\\x{0020}-\\x{007E}]"));
    bool containsNonASCII = str.contains(exp);
    return !containsNonASCII;
}

void memories::timeout()
{
    if (rigCaps->manufacturer == manufYaesu)
    {
        timeoutTimer.stop();
        ui->group->setEnabled(true);
        if (!ui->disableEditing->isChecked())
        {
            ui->table->setEditTriggers(QAbstractItemView::DoubleClicked);
        }
        // We have reached the last memory and Yaesu usefully doesn't respond to non-existant ones!
        startup=false;
    } else {
        if (timeoutCount < 3 )
        {
            qInfo(logRig()) << "Timeout receiving memory:" << (lastMemoryRequested & 0xffff) << "in group" << (lastMemoryRequested >> 16 & 0xffff);
            if (ui->group->currentData().toInt() == MEMORY_SATGROUP) {
                queue->add(priorityImmediate,queueItem(funcSatelliteMemory,QVariant::fromValue<ushort>(lastMemoryRequested & 0xffff)));
            } else {
                queue->add(priorityImmediate,queueItem(funcMemoryContents,QVariant::fromValue<uint>(lastMemoryRequested)));
            }
            timeoutTimer.start(MEMORY_TIMEOUT);
            timeoutCount++;
        } else {
            timeoutCount=0;
            //ui->loadingMemories->setVisible(false);
            timeoutTimer.stop();
            ui->group->setEnabled(true);
            if (!ui->disableEditing->isChecked())
            {
                ui->table->setEditTriggers(QAbstractItemView::DoubleClicked);
            }
            QMessageBox::information(this,"Timeout", "Timeout receiving memories, check rig connection", QMessageBox::Ok);
        }
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
        ui->table->blockSignals(true);

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
        progress->setRange(rigCaps->memStart,rigCaps->memories);
        while (readCSVRow(input, &row)) {
            progress->setValue(rows);
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
                connect(recall, &QPushButton::clicked, this, [=]() {
                    recallMem(row[0].toInt());
                });
            }
            // rownum is now the row we need to work on.

            int colnum=1;

            for (int i=0; i<row.size();i++)
            {
                if (!ui->checkAllFields->isChecked()) {
                    while (colnum < ui->table->columnCount() && ui->table->isColumnHidden(colnum)) {
                        colnum++;
                    }
                }

                if (colnum < ui->table->columnCount() && row.size() > i)
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
                    case columnAntenna:
                        if (!data.startsWith("ANT")) data = "ANT "+data;
                    default:
                        break;
                    }

                    ui->table->model()->setData(ui->table->model()->index(rownum,colnum),data);

                    if (colnum == lastcol) {
                        on_table_cellChanged(rownum,colnum); // store this row.
                    }
                    colnum++;
                }
            }
        }

        ui->table->blockSignals(false);
        progress->setValue(rigCaps->memories);
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
                if (!ui->table->isColumnHidden(i) || ui->checkAllFields->isChecked())
                {
                    output << "\"" << ui->table->horizontalHeaderItem(i)->text() << "\"";
                    if (i < ui->table->columnCount()-1)
                        output << ",";
                    else
                        output << "\n";
                }
                else if (i == ui->table->columnCount()-1) {
                    output << "\n";
                }
            }

            for(int i=0;i<ui->table->rowCount();i++) {
                for(int j=1;j<ui->table->columnCount();j++) {
                    if (!ui->table->isColumnHidden(j) || ui->checkAllFields->isChecked())
                    {
                        output << "\"" << ((ui->table->item(i,j) == NULL) ? "" : ui->table->item(i,j)->text()) << "\"";
                        if (j < ui->table->columnCount()-1)
                            output << ",";
                        else
                            output << "\n";
                    }
                    else if (j == ui->table->columnCount()-1) {
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

void memories::on_disableEditing_toggled(bool dis)
{
    if (dis) {
        ui->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    }
    else {
        ui->table->setEditTriggers(QAbstractItemView::DoubleClicked);
    }
    ui->csvImport->setEnabled(!dis);
    ui->table->editing(!dis);
}

void memories::on_scanButton_toggled(bool scan)
{
    // Change the priority of funcSelectedFreq so that it updates quicker during scans!
    static queuePriority prio = priorityNone;
    if (scan) {
        ui->scanButton->setText("Stop Scan");
        prio=queue->changePriority(priorityHighest,funcSelectedFreq,0);

    } else {
        ui->scanButton->setText("Start Scan");
        if (prio != priorityNone)
            queue->changePriority(prio,funcSelectedFreq,0);
    }
    queue->add(priorityImmediate,queueItem(funcScanning,QVariant::fromValue<uchar>(uchar(scan))));
}

void memories::configColumns(int row, modeInfo mode, quint8 split) {

    enableCell(row,columnTone,false);
    enableCell(row,columnToneMode,false);
    enableCell(row,columnTSQL,false);
    enableCell(row,columnDTCS,false);
    enableCell(row,columnDTCSPolarity,false);
    enableCell(row,columnDVSquelch,false);
    enableCell(row,columnDSQL,false);
    enableCell(row,columnR1,false);
    enableCell(row,columnR2,false);
    enableCell(row,columnUR,false);
    enableCell(row,columnP25Nac,false);
    enableCell(row,columnP25Sql,false);
    enableCell(row,columnDPmrSql,false);
    enableCell(row,columnDPmrComid,false);
    enableCell(row,columnDPmrCc,false);
    enableCell(row,columnDPmrSCRM,false);
    enableCell(row,columnDPmrKey,false);
    enableCell(row,columnNxdnSql,false);
    enableCell(row,columnNxdnRan,false);
    enableCell(row,columnNxdnEnc,false);
    enableCell(row,columnNxdnKey,false);
    enableCell(row,columnDcrSql,false);
    enableCell(row,columnDcrUc,false);
    enableCell(row,columnDcrEnc,false);
    enableCell(row,columnDcrKey,false);


    switch (mode.mk)
    {
    case modeFM:
        enableCell(row,columnTone,true);
        enableCell(row,columnToneMode,true);
        enableCell(row,columnTSQL,true);
        enableCell(row,columnDTCS,true);
        enableCell(row,columnDTCSPolarity,true);
        break;
    case modeP25:
        enableCell(row,columnP25Nac,true);
        enableCell(row,columnP25Sql,true);
        break;
    case modedPMR:
        enableCell(row,columnDPmrSql,true);
        enableCell(row,columnDPmrComid,true);
        enableCell(row,columnDPmrCc,true);
        enableCell(row,columnDPmrSCRM,true);
        enableCell(row,columnDPmrKey,true);
        break;
    case modeNXDN_N:
    case modeNXDN_VN:
        enableCell(row,columnNxdnSql,true);
        enableCell(row,columnNxdnRan,true);
        enableCell(row,columnNxdnEnc,true);
        enableCell(row,columnNxdnKey,true);
        break;
    case modeDCR:
        enableCell(row,columnDcrSql,true);
        enableCell(row,columnDcrUc,true);
        enableCell(row,columnDcrEnc,true);
        enableCell(row,columnDcrKey,true);
        break;
    case modeDV:
        enableCell(row,columnDVSquelch,true);
        enableCell(row,columnDSQL,true);
        enableCell(row,columnR1,true);
        enableCell(row,columnR2,true);
        enableCell(row,columnUR,true);
        break;
    default:
        break;
    }

    if (rigCaps->manufacturer==manufYaesu)
    {
        enableCell(row,columnFrequencyB,bool(split>0));
    }
}

void memories::configColumnsB(int row, modeInfo mode) {

    enableCell(row,columnToneB,false);
    enableCell(row,columnToneModeB,false);
    enableCell(row,columnTSQLB,false);
    enableCell(row,columnDTCSB,false);
    enableCell(row,columnDTCSPolarityB,false);
    enableCell(row,columnDVSquelchB,false);
    enableCell(row,columnDSQLB,false);
    enableCell(row,columnR1B,false);
    enableCell(row,columnR2B,false);
    enableCell(row,columnURB,false);

    switch (mode.mk)
    {
    case modeFM:
        enableCell(row,columnToneB,true);
        enableCell(row,columnToneModeB,true);
        enableCell(row,columnTSQLB,true);
        enableCell(row,columnDTCSB,true);
        enableCell(row,columnDTCSPolarityB,true);
        break;
    case modeDV:
        enableCell(row,columnDVSquelchB,true);
        enableCell(row,columnDSQLB,true);
        enableCell(row,columnR1B,true);
        enableCell(row,columnR2B,true);
        enableCell(row,columnURB,true);
        break;
    default:
        break;
    }
}
