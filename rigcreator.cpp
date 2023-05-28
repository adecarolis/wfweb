#include <QDebug>
#include "logcategories.h"

#include "rigcreator.h"
#include "ui_rigcreator.h"

rigCreator::rigCreator(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::rigCreator)
{
    ui->setupUi(this);

    qInfo() << "Creating instance of rigCreator()";
    commandsList = new tableCombobox(createModel(commandsModel, funcString),true,ui->commands);
    ui->commands->setItemDelegateForColumn(0, commandsList);

    ui->commands->setColumnWidth(0,120);
    ui->commands->setColumnWidth(1,100);
    ui->commands->setColumnWidth(2,50);
    ui->commands->setColumnWidth(3,50);
    ui->commands->setColumnWidth(4,40);

    connect(ui->commands,SIGNAL(rowAdded(int)),this,SLOT(commandRowAdded(int)));
}

void rigCreator::commandRowAdded(int row)
{
    // Create a widget that will contain a checkbox
    QWidget *checkBoxWidget = new QWidget();
    QCheckBox *checkBox = new QCheckBox();      // We declare and initialize the checkbox
    checkBox->setObjectName("check");
    QHBoxLayout *layoutCheckBox = new QHBoxLayout(checkBoxWidget); // create a layer with reference to the widget
    layoutCheckBox->addWidget(checkBox);            // Set the checkbox in the layer
    layoutCheckBox->setAlignment(Qt::AlignCenter);  // Center the checkbox
    layoutCheckBox->setContentsMargins(0,0,0,0);    // Set the zero padding
    ui->commands->setCellWidget(row,4, checkBoxWidget);
}


rigCreator::~rigCreator()
{
    qInfo() << "Deleting instance of rigCreator()";
    delete ui;
}

void rigCreator::on_defaultRigs_clicked(bool clicked)
{
    Q_UNUSED(clicked)


     QString appdata = QCoreApplication::applicationDirPath();

#ifdef Q_OS_LINUX
     appdata += "/../share/wfview/rigs";
     QString file = QFileDialog::getOpenFileName(this,"Select Rig Filename",appdata,"Rig Files (*.rig)",nullptr,QFileDialog::DontUseNativeDialog);
#else
     appdata +="/rigs";
     QString file = QFileDialog::getOpenFileName(this,"Select Rig Filename",appdata,"Rig Files (*.rig)");
#endif

    if (!file.isEmpty())
    {
        loadRigFile(file);
    }
}

void rigCreator::on_loadFile_clicked(bool clicked)
{
    Q_UNUSED(clicked)
    QString appdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(appdata);
    if (!dir.exists()) {
        dir.mkpath(appdata);
    }

    if (!dir.exists("rigs")) {
        dir.mkdir("rigs");
    }

#ifdef Q_OS_LINUX
    QString file = QFileDialog::getOpenFileName(this,"Select Rig Filename",appdata+"/rigs","Rig Files (*.rig)",nullptr,QFileDialog::DontUseNativeDialog);
#else
    QString file = QFileDialog::getOpenFileName(this,"Select Rig Filename",appdata+"/rigs","Rig Files (*.rig)");
#endif

    if (!file.isEmpty())
    {
        loadRigFile(file);
    }
}


void rigCreator::loadRigFile(QString file)
{

    ui->loadFile->setEnabled(false);
    ui->defaultRigs->setEnabled(false);
    this->currentFile = file;
    QSettings* settings = new QSettings(file, QSettings::Format::IniFormat);

    if (!settings->childGroups().contains("Rig"))
    {
        QFileInfo info(file);
        QMessageBox msgBox;
        msgBox.setText("Not a rig definition");
        msgBox.setInformativeText(QString("File %0 does not appear to be a valid Rig definition file").arg(info.fileName()));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        delete settings;
        return;
    }

    settings->beginGroup("Rig");
    int manuf=ui->manufacturer->findText(settings->value("Manufacturer","Icom").toString());
    ui->manufacturer->setCurrentIndex(manuf);
    ui->model->setText(settings->value("Model","").toString());
    ui->civAddress->setText(QString("%1").arg(settings->value("CIVAddress",0).toInt(),2,16));
    ui->rigctldModel->setText(settings->value("RigCtlDModel","").toString());
    ui->seqMax->setText(settings->value("SpectrumSeqMax","").toString());
    ui->ampMax->setText(settings->value("SpectrumAmpMax","").toString());
    ui->lenMax->setText(settings->value("SpectrumLenMax","").toString());

    ui->hasSpectrum->setChecked(settings->value("HasSpectrum",false).toBool());
    ui->hasLAN->setChecked(settings->value("HasLAN",false).toBool());
    ui->hasEthernet->setChecked(settings->value("HasEthernet",false).toBool());
    ui->hasWifi->setChecked(settings->value("HasWiFi",false).toBool());
    ui->hasTransmit->setChecked(settings->value("HasTransmit",false).toBool());
    ui->hasFDComms->setChecked(settings->value("HasFDComms",false).toBool());
    ui->hasCommand29->setChecked(settings->value("HasCommand29",false).toBool());

    ui->memGroups->setText(settings->value("MemGroups","0").toString());
    ui->memories->setText(settings->value("Memories","0").toString());
    ui->memStart->setText(settings->value("MemStart","1").toString());
    ui->memoryFormat->setText(settings->value("MemFormat","").toString());
    ui->satMemories->setText(settings->value("SatMemories","0").toString());
    ui->satelliteFormat->setText(settings->value("SatFormat","").toString());

    ui->commands->clearContents();
    ui->commands->model()->removeRows(0,ui->commands->rowCount());
    int numCommands = settings->beginReadArray("Commands");
    if (numCommands == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numCommands; c++)
        {
            settings->setArrayIndex(c);
            ui->commands->insertRow(ui->commands->rowCount());

            // Create a widget that will contain a checkbox
            QWidget *checkBoxWidget = new QWidget();
            QCheckBox *checkBox = new QCheckBox();      // We declare and initialize the checkbox
            checkBox->setObjectName("check");
            QHBoxLayout *layoutCheckBox = new QHBoxLayout(checkBoxWidget); // create a layer with reference to the widget
            layoutCheckBox->addWidget(checkBox);            // Set the checkbox in the layer
            layoutCheckBox->setAlignment(Qt::AlignCenter);  // Center the checkbox
            layoutCheckBox->setContentsMargins(0,0,0,0);    // Set the zero padding

            if (settings->value("Command29",false).toBool()) {
                checkBox->setChecked(true);
            } else {
                checkBox->setChecked(false);
            }
            ui->commands->model()->setData(ui->commands->model()->index(c,0),settings->value("Type", "").toString());
            ui->commands->model()->setData(ui->commands->model()->index(c,1),settings->value("String", "").toString());
            ui->commands->model()->setData(ui->commands->model()->index(c,2),QString::number(settings->value("Min", 0).toInt(),16));
            ui->commands->model()->setData(ui->commands->model()->index(c,3),QString::number(settings->value("Max", 0).toInt(),16));
            ui->commands->setCellWidget(c,4, checkBoxWidget);

        }
        settings->endArray();
    }

    ui->spans->clearContents();
    ui->spans->model()->removeRows(0,ui->spans->rowCount());
    int numSpans = settings->beginReadArray("Spans");
    if (numSpans == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numSpans; c++)
        {
            settings->setArrayIndex(c);
            ui->spans->insertRow(ui->spans->rowCount());
            ui->spans->model()->setData(ui->spans->model()->index(c,0),QString::number(settings->value("Num", 0).toUInt()).rightJustified(2,'0'));
            ui->spans->model()->setData(ui->spans->model()->index(c,1),settings->value("Name", "").toString());
            ui->spans->model()->setData(ui->spans->model()->index(c,2),settings->value("Freq", 0U).toUInt());

        }
        settings->endArray();
    }

    ui->inputs->clearContents();
    ui->inputs->model()->removeRows(0,ui->inputs->rowCount());
    int numInputs = settings->beginReadArray("Inputs");
    if (numInputs == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numInputs; c++)
        {
            settings->setArrayIndex(c);
            ui->inputs->insertRow(ui->inputs->rowCount());
            ui->inputs->model()->setData(ui->inputs->model()->index(c,0),QString::number(settings->value("Num", 0).toUInt()).rightJustified(2,'0'));
            ui->inputs->model()->setData(ui->inputs->model()->index(c,1),QString::number(settings->value("Reg", 0).toUInt(),16).rightJustified(2,'0'));
            ui->inputs->model()->setData(ui->inputs->model()->index(c,2),settings->value("Name", "").toString());
        }
        settings->endArray();
    }

    ui->bands->clearContents();
    ui->bands->model()->removeRows(0,ui->bands->rowCount());
    int numBands = settings->beginReadArray("Bands");
    if (numBands == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numBands; c++)
        {
            settings->setArrayIndex(c);
            ui->bands->insertRow(ui->bands->rowCount());
            ui->bands->model()->setData(ui->bands->model()->index(c,0),QString::number(settings->value("Num", 0).toUInt()).rightJustified(2,'0'));
            ui->bands->model()->setData(ui->bands->model()->index(c,1),QString::number(settings->value("BSR", 0).toUInt(),16).rightJustified(2,'0'));
            ui->bands->model()->setData(ui->bands->model()->index(c,2),settings->value("Name", "").toString());
            ui->bands->model()->setData(ui->bands->model()->index(c,3),settings->value("Start", 0ULL).toString());
            ui->bands->model()->setData(ui->bands->model()->index(c,4),settings->value("End", 0ULL).toString());
            ui->bands->model()->setData(ui->bands->model()->index(c,5),settings->value("Range", 0.0).toString());
            ui->bands->model()->setData(ui->bands->model()->index(c,6),settings->value("MemoryGroup", -1).toString());
        }
        settings->endArray();
    }

    ui->modes->clearContents();
    ui->modes->model()->removeRows(0,ui->modes->rowCount());
    int numModes = settings->beginReadArray("Modes");
    if (numModes == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numModes; c++)
        {
            settings->setArrayIndex(c);
            ui->modes->insertRow(ui->modes->rowCount());
            ui->modes->model()->setData(ui->modes->model()->index(c,0),QString::number(settings->value("Num", 0).toUInt()).rightJustified(2,'0'));
            ui->modes->model()->setData(ui->modes->model()->index(c,1),QString::number(settings->value("Reg", 0).toUInt(),16).rightJustified(2,'0'));
            ui->modes->model()->setData(ui->modes->model()->index(c,2),settings->value("Name", "").toString());
            ui->modes->model()->setData(ui->modes->model()->index(c,3),settings->value("BW", 0).toString().toInt());
        }
        settings->endArray();
    }

    ui->attenuators->clearContents();
    ui->attenuators->model()->removeRows(0,ui->attenuators->rowCount());
    int numAttenuators = settings->beginReadArray("Attenuators");
    if (numAttenuators == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numAttenuators; c++)
        {
            settings->setArrayIndex(c);
            ui->attenuators->insertRow(ui->attenuators->rowCount());
            ui->attenuators->model()->setData(ui->attenuators->model()->index(c,0),QString::number(settings->value("dB", 0).toUInt(),16).rightJustified(2,'0'));
        }
        settings->endArray();
    }

    ui->preamps->clearContents();
    ui->preamps->model()->removeRows(0,ui->preamps->rowCount());
    int numPreamps = settings->beginReadArray("Preamps");
    if (numPreamps == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numPreamps; c++)
        {
            settings->setArrayIndex(c);
            ui->preamps->insertRow(ui->preamps->rowCount());
            ui->preamps->model()->setData(ui->preamps->model()->index(c,0),QString::number(settings->value("Num", 0).toUInt(),16).rightJustified(2,'0'));
            ui->preamps->model()->setData(ui->preamps->model()->index(c,1),settings->value("Name", "").toString());
        }
        settings->endArray();
    }

    ui->antennas->clearContents();
    ui->antennas->model()->removeRows(0,ui->antennas->rowCount());
    int numAntennas = settings->beginReadArray("Antennas");
    if (numAntennas == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numAntennas; c++)
        {
            settings->setArrayIndex(c);
            ui->antennas->insertRow(ui->antennas->rowCount());
            ui->antennas->model()->setData(ui->antennas->model()->index(c,0),QString::number(settings->value("Num", 0).toUInt(),16).rightJustified(2,'0'));
            ui->antennas->model()->setData(ui->antennas->model()->index(c,1),settings->value("Name", "").toString());
        }
        settings->endArray();
    }

    ui->tuningSteps->clearContents();
    ui->tuningSteps->model()->removeRows(0,ui->tuningSteps->rowCount());
    int numSteps = settings->beginReadArray("Tuning Steps");
    if (numSteps == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numSteps; c++)
        {
            settings->setArrayIndex(c);
            ui->tuningSteps->insertRow(ui->tuningSteps->rowCount());
            ui->tuningSteps->model()->setData(ui->tuningSteps->model()->index(c,0),QString::number(settings->value("Num", 0).toUInt(),16).rightJustified(2,'0'));
            ui->tuningSteps->model()->setData(ui->tuningSteps->model()->index(c,1),settings->value("Name", "").toString());
            ui->tuningSteps->model()->setData(ui->tuningSteps->model()->index(c,2),settings->value("Hz", 0ULL).toString());
        }
        settings->endArray();
    }

    ui->filters->clearContents();
    ui->filters->model()->removeRows(0,ui->filters->rowCount());
    int numFilters = settings->beginReadArray("Filters");
    if (numFilters == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numFilters; c++)
        {
            settings->setArrayIndex(c);
            ui->filters->insertRow(ui->filters->rowCount());
            ui->filters->model()->setData(ui->filters->model()->index(c,0),settings->value("Num", 0).toString());
            ui->filters->model()->setData(ui->filters->model()->index(c,1),settings->value("Name", "").toString());
            ui->filters->model()->setData(ui->filters->model()->index(c,2),QString::number(settings->value("Modes", 0xffffffff).toUInt(),16));
        }
        settings->endArray();
    }

    settings->endGroup();
    delete settings;

    // Connect signals to find out if changed.
    connect(ui->antennas,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->attenuators,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->bands,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->commands,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->filters,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->inputs,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->modes,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->preamps,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->spans,SIGNAL(cellChanged(int,int)),SLOT(changed()));
    connect(ui->hasCommand29,SIGNAL(stateChanged(int)),SLOT(changed()));
    connect(ui->hasEthernet,SIGNAL(stateChanged(int)),SLOT(changed()));
    connect(ui->hasFDComms,SIGNAL(stateChanged(int)),SLOT(changed()));
    connect(ui->hasLAN,SIGNAL(stateChanged(int)),SLOT(changed()));
    connect(ui->hasSpectrum,SIGNAL(stateChanged(int)),SLOT(changed()));
    connect(ui->hasTransmit,SIGNAL(stateChanged(int)),SLOT(changed()));
    connect(ui->hasWifi,SIGNAL(stateChanged(int)),SLOT(changed()));
    connect(ui->civAddress,SIGNAL(editingFinished()),SLOT(changed()));
    connect(ui->rigctldModel,SIGNAL(editingFinished()),SLOT(changed()));
    connect(ui->model,SIGNAL(editingFinished()),SLOT(changed()));
//    connect(ui->manufacturer,SIGNAL(editingFinished()),SLOT(changed()));
    connect(ui->memGroups,SIGNAL(editingFinished()),SLOT(changed()));
    connect(ui->memStart,SIGNAL(editingFinished()),SLOT(changed()));
    connect(ui->memories,SIGNAL(editingFinished()),SLOT(changed()));
    connect(ui->memoryFormat,SIGNAL(editingFinished()),SLOT(changed()));
    connect(ui->satMemories,SIGNAL(editingFinished()),SLOT(changed()));
    connect(ui->satelliteFormat,SIGNAL(editingFinished()),SLOT(changed()));

    settingsChanged = false;
}

void rigCreator::changed()
{
    settingsChanged = true;
}
void rigCreator::on_saveFile_clicked(bool clicked)
{
    Q_UNUSED(clicked)

    QString appdata = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    QDir dir(appdata);
    if (!dir.exists()) {
        dir.mkpath(appdata);
    }

    if (!dir.exists("rigs")) {
        dir.mkdir("rigs");
    }

    QFileInfo fileInfo(currentFile);
#ifdef Q_OS_LINUX
    QString file = QFileDialog::getSaveFileName(this,"Select Rig Filename",appdata+"/rigs/"+fileInfo.fileName(),"Rig Files (*.rig)",nullptr,QFileDialog::DontUseNativeDialog);
#else
    QString file = QFileDialog::getSaveFileName(this,"Select Rig Filename",appdata+"/rigs/"+fileInfo.fileName(),"Rig Files (*.rig)");
#endif

    if (!file.isEmpty())
    {
        saveRigFile(file);
    }
}

void rigCreator::saveRigFile(QString file)
{

    QSettings* settings = new QSettings(file, QSettings::Format::IniFormat);

    settings->setValue("Version", QString(WFVIEW_VERSION));
    settings->remove("Rig");
    settings->sync();
    settings->beginGroup("Rig");

    settings->setValue("Manufacturer",ui->manufacturer->currentText());
    settings->setValue("Model",ui->model->text());
    settings->setValue("CIVAddress",ui->civAddress->text().toInt(nullptr,16));
    settings->setValue("RigCtlDModel",ui->rigctldModel->text().toInt());
    settings->setValue("SpectrumSeqMax",ui->seqMax->text().toInt());
    settings->setValue("SpectrumAmpMax",ui->ampMax->text().toInt());
    settings->setValue("SpectrumLenMax",ui->lenMax->text().toInt());

    settings->setValue("HasSpectrum",ui->hasSpectrum->isChecked());
    settings->setValue("HasLAN",ui->hasLAN->isChecked());
    settings->setValue("HasEthernet",ui->hasEthernet->isChecked());
    settings->setValue("HasWiFi",ui->hasWifi->isChecked());
    settings->setValue("HasTransmit",ui->hasTransmit->isChecked());
    settings->setValue("HasFDComms",ui->hasFDComms->isChecked());
    settings->setValue("HasCommand29",ui->hasCommand29->isChecked());

    settings->setValue("MemGroups",ui->memGroups->text().toInt());
    settings->setValue("Memories",ui->memories->text().toInt());
    settings->setValue("MemStart",ui->memStart->text().toInt());
    settings->setValue("MemFormat",ui->memoryFormat->text());
    settings->setValue("SatMemories",ui->satMemories->text().toInt());
    settings->setValue("SatFormat",ui->satelliteFormat->text());


    //settings->remove("Commands");
    settings->beginWriteArray("Commands");
    for (int n = 0; n<ui->commands->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Type", (ui->commands->item(n,0) == NULL) ? "" : ui->commands->item(n,0)->text());
        settings->setValue("String", (ui->commands->item(n,1) == NULL) ? "" : ui->commands->item(n,1)->text());
        settings->setValue("Min", (ui->commands->item(n,2) == NULL) ? 0 : ui->commands->item(n,2)->text().toInt(nullptr,16));
        settings->setValue("Max", (ui->commands->item(n,3) == NULL) ? 0 : ui->commands->item(n,3)->text().toInt(nullptr,16));

        QCheckBox* chk = ui->commands->cellWidget(n,4)->findChild<QCheckBox*>();
        if (chk != nullptr)
        {
            settings->setValue("Command29", chk->isChecked());
        }

    }
    settings->endArray();

    //settings->remove("Spans");
    settings->beginWriteArray("Spans");

    for (int n = 0; n<ui->spans->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num", (ui->spans->item(n,0) == NULL) ? 0 : ui->spans->item(n,0)->text().toUInt(nullptr,16));
        settings->setValue("Name",(ui->spans->item(n,1) == NULL) ? "" : ui->spans->item(n,1)->text());
        settings->setValue("Freq",(ui->spans->item(n,2) == NULL) ? 0U : ui->spans->item(n,2)->text().toUInt());
    }
    settings->endArray();

    //settings->remove("Inputs");
    settings->beginWriteArray("Inputs");

    for (int n = 0; n<ui->inputs->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num", (ui->inputs->item(n,0) == NULL) ? 0 : ui->inputs->item(n,0)->text().toUInt());
        settings->setValue("Reg", (ui->inputs->item(n,1) == NULL) ? 0 : ui->inputs->item(n,1)->text().toUInt(nullptr,16));
        settings->setValue("Name", (ui->inputs->item(n,2) == NULL) ? "" : ui->inputs->item(n,2)->text());
    }
    settings->endArray();

    //settings->remove("Bands");
    settings->beginWriteArray("Bands");
    for (int n = 0; n<ui->bands->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num", (ui->bands->item(n,0) == NULL) ? 0 : ui->bands->item(n,0)->text().toUInt() );
        settings->setValue("BSR", (ui->bands->item(n,1) == NULL) ? 0 : ui->bands->item(n,1)->text().toUInt(nullptr,16) );
        settings->setValue("Name", (ui->bands->item(n,2) == NULL) ? "" : ui->bands->item(n,2)->text());
        settings->setValue("Start", (ui->bands->item(n,3) == NULL) ? 0ULL : ui->bands->item(n,3)->text().toULongLong() );
        settings->setValue("End", (ui->bands->item(n,4) == NULL) ? 0ULL : ui->bands->item(n,4)->text().toULongLong() );
        settings->setValue("Range", (ui->bands->item(n,5) == NULL) ? 0.0 : ui->bands->item(n,5)->text().toDouble() );
        settings->setValue("MemoryGroup", (ui->bands->item(n,6) == NULL) ? -1 : ui->bands->item(n,6)->text().toInt() );
    }
    settings->endArray();

    //settings->remove("Modes");
    settings->beginWriteArray("Modes");

    for (int n = 0; n<ui->modes->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num", (ui->modes->item(n,0) == NULL) ? 0 : ui->modes->item(n,0)->text().toUInt());
        settings->setValue("Reg", (ui->modes->item(n,1) == NULL) ? 0 : ui->modes->item(n,1)->text().toInt(nullptr,16));
        settings->setValue("Name",(ui->modes->item(n,2) == NULL) ? "" : ui->modes->item(n,2)->text());
        settings->setValue("BW",(ui->modes->item(n,3) == NULL) ? 0 : ui->modes->item(n,3)->text().toInt());
    }
    settings->endArray();

    //settings->remove("Attenuators");
    settings->beginWriteArray("Attenuators");
    for (int n = 0; n<ui->attenuators->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("dB",(ui->attenuators->item(n,0) == NULL) ? 0 :  ui->attenuators->item(n,0)->text().toUInt(nullptr,16));
    }
    settings->endArray();

    //settings->remove("Preamps");
    settings->beginWriteArray("Preamps");
    for (int n = 0; n<ui->preamps->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num",(ui->preamps->item(n,0) == NULL) ? 0 :  ui->preamps->item(n,0)->text().toUInt(nullptr,16));
        settings->setValue("Name",(ui->preamps->item(n,1) == NULL) ? "" :  ui->preamps->item(n,1)->text());
    }
    settings->endArray();

    //settings->remove("Antennas");
    settings->beginWriteArray("Antennas");
    for (int n = 0; n<ui->antennas->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num",(ui->antennas->item(n,0) == NULL) ? 0 :  ui->antennas->item(n,0)->text().toUInt(nullptr,16));
        settings->setValue("Name",(ui->antennas->item(n,1) == NULL) ? "" :  ui->antennas->item(n,1)->text());
    }
    settings->endArray();

    //settings->remove("Tuning Steps");
    settings->beginWriteArray("Tuning Steps");
    for (int n = 0; n<ui->tuningSteps->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num",(ui->tuningSteps->item(n,0) == NULL) ? 0 :  ui->tuningSteps->item(n,0)->text().toUInt(nullptr,16));
        settings->setValue("Name",(ui->tuningSteps->item(n,1) == NULL) ? "" :  ui->tuningSteps->item(n,1)->text());
        settings->setValue("Hz",(ui->tuningSteps->item(n,2) == NULL) ? 0ULL :  ui->tuningSteps->item(n,2)->text().toULongLong());
    }
    settings->endArray();

    //settings->remove("Filters");
    settings->beginWriteArray("Filters");
    for (int n = 0; n<ui->filters->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num",(ui->filters->item(n,0) == NULL) ? 0 :  ui->filters->item(n,0)->text().toUInt(nullptr,16));
        settings->setValue("Name",(ui->filters->item(n,1) == NULL) ? "" :  ui->filters->item(n,1)->text());
        settings->setValue("Modes",(ui->filters->item(n,2) == NULL) ? 0xffffffff : ui->filters->item(n,2)->text().toUInt(nullptr,16));
    }
    settings->endArray();

    settings->endGroup();
    settings->sync();

    delete settings;

    settingsChanged = false;

}



// Create model for comboBox, takes un-initialized model object and populates it.
// This will be deleted by the comboBox on destruction.
QStandardItemModel* rigCreator::createModel(QStandardItemModel* model, QString strings[])
{

    model = new QStandardItemModel();

    for (int i=0; i < NUMFUNCS;i++)
    {
        if (!strings[i].startsWith('-')) {
            QStandardItem *itemName = new QStandardItem(strings[i]);
            QStandardItem *itemId = new QStandardItem(i);

            QList<QStandardItem*> row;
            row << itemName << itemId;

            model->appendRow(row);
        }
    }

    return model;
}

void rigCreator::on_hasCommand29_toggled(bool checked)
{
    ui->commands->setColumnHidden(4,!checked);
}


void rigCreator::closeEvent(QCloseEvent *event)
{

    if (settingsChanged)
    {
        // Settings have changed since last save
        qInfo() << "Settings have changed since last save";
        int reply = QMessageBox::question(this,"rig creator","Changes will be lost!",QMessageBox::Cancel |QMessageBox::Ok);
        if (reply == QMessageBox::Cancel)
        {
            event->ignore();
        }
    }
}
