#include <QDebug>
#include "logcategories.h"

#include "rigcreator.h"
#include "ui_rigcreator.h"

rigCreator::rigCreator(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::rigCreator)
{
    ui->setupUi(this);
    commandsList = new tableCombobox(createModel(funcString),true,ui->commands);
    ui->commands->setItemDelegateForColumn(0, commandsList);

    ui->commands->setColumnWidth(0,145);
    ui->commands->setColumnWidth(1,125);
    ui->commands->setColumnWidth(2,50);
    ui->commands->setColumnWidth(3,50);

}

rigCreator::~rigCreator()
{
    delete ui;
}

void rigCreator::on_defaultRigs_clicked(bool clicked)
{
    Q_UNUSED(clicked)

#ifdef Q_OS_LINUX
    QString appdata = "/usr/local/share/wfview/rigs";
    QString file = QFileDialog::getOpenFileName(this,"Select Rig Filename",appdata,"Rig Files (*.rig)",nullptr,QFileDialog::DontUseNativeDialog);
#else
    QString appdata = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/rigs";
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
#ifdef DEVMODE
    QString appdata = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#else
    QString appdata = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/wfview";
#endif
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

    ui->commands->setRowCount(0);
    int numCommands = settings->beginReadArray("Commands");
    if (numCommands == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numCommands; c++)
        {
            settings->setArrayIndex(c);
            ui->commands->insertRow(ui->commands->rowCount());
            ui->commands->model()->setData(ui->commands->model()->index(c,0),settings->value("Type", "").toString());
            ui->commands->model()->setData(ui->commands->model()->index(c,1),settings->value("String", "").toString());
            ui->commands->model()->setData(ui->commands->model()->index(c,2),settings->value("Min", 0).toInt());
            ui->commands->model()->setData(ui->commands->model()->index(c,3),settings->value("Max", 0).toInt());

        }
        settings->endArray();
    }

    ui->spans->setRowCount(0);
    int numSpans = settings->beginReadArray("Spans");
    if (numSpans == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numSpans; c++)
        {
            settings->setArrayIndex(c);
            ui->spans->insertRow(ui->spans->rowCount());
            ui->spans->model()->setData(ui->spans->model()->index(c,0),QString::number(settings->value("Num", 0).toInt()).rightJustified(2,'0'));
            ui->spans->model()->setData(ui->spans->model()->index(c,1),settings->value("Name", "").toString());
            ui->spans->model()->setData(ui->spans->model()->index(c,2),settings->value("Freq", 0U).toUInt());

        }
        settings->endArray();
    }

    ui->inputs->setRowCount(0);
    int numInputs = settings->beginReadArray("Inputs");
    if (numInputs == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numInputs; c++)
        {
            settings->setArrayIndex(c);
            ui->inputs->insertRow(ui->inputs->rowCount());
            ui->inputs->model()->setData(ui->inputs->model()->index(c,0),QString::number(settings->value("Num", 0).toInt()).rightJustified(2,'0'));
            ui->inputs->model()->setData(ui->inputs->model()->index(c,1),settings->value("Name", "").toString());

        }
        settings->endArray();
    }

    ui->bands->setRowCount(0);
    int numBands = settings->beginReadArray("Bands");
    if (numBands == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numBands; c++)
        {
            settings->setArrayIndex(c);
            ui->bands->insertRow(ui->bands->rowCount());
            ui->bands->model()->setData(ui->bands->model()->index(c,0),QString::number(settings->value("Num", 0).toInt()).rightJustified(2,'0'));
            ui->bands->model()->setData(ui->bands->model()->index(c,1),settings->value("BSR", 0).toString());
            ui->bands->model()->setData(ui->bands->model()->index(c,2),settings->value("Name", "").toString());
            ui->bands->model()->setData(ui->bands->model()->index(c,3),settings->value("Start", 0ULL).toString());
            ui->bands->model()->setData(ui->bands->model()->index(c,4),settings->value("End", 0ULL).toString());
            ui->bands->model()->setData(ui->bands->model()->index(c,5),settings->value("Range", 0.0).toString());
        }
        settings->endArray();
    }

    ui->modes->setRowCount(0);
    int numModes = settings->beginReadArray("Modes");
    if (numModes == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numModes; c++)
        {
            settings->setArrayIndex(c);
            ui->modes->insertRow(ui->modes->rowCount());
            ui->modes->model()->setData(ui->modes->model()->index(c,0),QString::number(settings->value("Num", 0).toInt()).rightJustified(2,'0'));
            ui->modes->model()->setData(ui->modes->model()->index(c,1),settings->value("Name", "").toString());
            ui->modes->model()->setData(ui->modes->model()->index(c,2),settings->value("BW", 0).toString().toInt());
        }
        settings->endArray();
    }

    ui->attenuators->setRowCount(0);
    int numAttenuators = settings->beginReadArray("Attenuators");
    if (numAttenuators == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numAttenuators; c++)
        {
            settings->setArrayIndex(c);
            ui->attenuators->insertRow(ui->attenuators->rowCount());
            ui->attenuators->model()->setData(ui->attenuators->model()->index(c,0),QString::number(settings->value("dB", 0).toInt()).rightJustified(2,'0'));
        }
        settings->endArray();
    }

    ui->preamps->setRowCount(0);
    int numPreamps = settings->beginReadArray("Preamps");
    if (numPreamps == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numPreamps; c++)
        {
            settings->setArrayIndex(c);
            ui->preamps->insertRow(ui->preamps->rowCount());
            ui->preamps->model()->setData(ui->preamps->model()->index(c,0),QString::number(settings->value("Num", 0).toInt()).rightJustified(2,'0'));
            ui->preamps->model()->setData(ui->preamps->model()->index(c,1),settings->value("Name", "").toString());
        }
        settings->endArray();
    }

    ui->antennas->setRowCount(0);
    int numAntennas = settings->beginReadArray("Antennas");
    if (numAntennas == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numAntennas; c++)
        {
            settings->setArrayIndex(c);
            ui->antennas->insertRow(ui->antennas->rowCount());
            ui->antennas->model()->setData(ui->antennas->model()->index(c,0),settings->value("Num", 0).toString());
            ui->antennas->model()->setData(ui->antennas->model()->index(c,1),settings->value("Name", "").toString());
        }
        settings->endArray();
    }

    ui->tuningSteps->setRowCount(0);
    int numSteps = settings->beginReadArray("Tuning Steps");
    if (numSteps == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numSteps; c++)
        {
            settings->setArrayIndex(c);
            ui->tuningSteps->insertRow(ui->tuningSteps->rowCount());
            ui->tuningSteps->model()->setData(ui->tuningSteps->model()->index(c,0),QString::number(settings->value("Num", 0ULL).toInt()).rightJustified(2,'0'));
            ui->tuningSteps->model()->setData(ui->tuningSteps->model()->index(c,1),settings->value("Hz", 0ULL).toString());
        }
        settings->endArray();
    }

    ui->filters->setRowCount(0);
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
            ui->filters->model()->setData(ui->filters->model()->index(c,2),QString::number(settings->value("Modes", 0).toUInt(),16));
        }
        settings->endArray();
    }

    settings->endGroup();
    delete settings;
}

void rigCreator::on_saveFile_clicked(bool clicked)
{
    Q_UNUSED(clicked)
#ifdef DEVMODE
    QString appdata = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#else
    QString appdata = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/wfview";
#endif

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

    bool ok=false; // Used for number conversions.

    QSettings* settings = new QSettings(file, QSettings::Format::IniFormat);

    settings->setValue("Version", QString(WFVIEW_VERSION));
    settings->remove("Rig");
    settings->sync();
    settings->beginGroup("Rig");

    settings->setValue("Manufacturer",ui->manufacturer->currentText());
    settings->setValue("Model",ui->model->text());
    settings->setValue("CIVAddress",ui->civAddress->text().toInt(&ok,16));
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

    //settings->remove("Commands");
    settings->beginWriteArray("Commands");
    for (int n = 0; n<ui->commands->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Type", (ui->commands->item(n,0) == NULL) ? "" : ui->commands->item(n,0)->text());
        settings->setValue("String", (ui->commands->item(n,1) == NULL) ? "" : ui->commands->item(n,1)->text());
        settings->setValue("Min", (ui->commands->item(n,2) == NULL) ? 0 : ui->commands->item(n,2)->text().toInt());
        settings->setValue("Max", (ui->commands->item(n,3) == NULL) ? 0 : ui->commands->item(n,3)->text().toInt());
    }
    settings->endArray();

    //settings->remove("Spans");
    settings->beginWriteArray("Spans");

    for (int n = 0; n<ui->spans->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num", (ui->spans->item(n,0) == NULL) ? 0 : ui->spans->item(n,0)->text().toInt());
        settings->setValue("Name",(ui->spans->item(n,1) == NULL) ? "" : ui->spans->item(n,1)->text());
        settings->setValue("Freq",(ui->spans->item(n,2) == NULL) ? 0U : ui->spans->item(n,2)->text().toUInt());
    }
    settings->endArray();

    //settings->remove("Inputs");
    settings->beginWriteArray("Inputs");

    for (int n = 0; n<ui->inputs->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num", (ui->inputs->item(n,0) == NULL) ? 0 : ui->inputs->item(n,0)->text().toInt());
        settings->setValue("Name", (ui->inputs->item(n,1) == NULL) ? "" : ui->inputs->item(n,1)->text());
    }
    settings->endArray();

    //settings->remove("Bands");
    settings->beginWriteArray("Bands");
    for (int n = 0; n<ui->bands->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num", (ui->bands->item(n,0) == NULL) ? 0 : ui->bands->item(n,0)->text().toInt() );
        settings->setValue("BSR", (ui->bands->item(n,1) == NULL) ? 0 : ui->bands->item(n,1)->text().toInt() );
        settings->setValue("Name", (ui->bands->item(n,2) == NULL) ? "" : ui->bands->item(n,2)->text());
        settings->setValue("Start", (ui->bands->item(n,3) == NULL) ? 0ULL : ui->bands->item(n,3)->text().toULongLong() );
        settings->setValue("End", (ui->bands->item(n,4) == NULL) ? 0ULL : ui->bands->item(n,4)->text().toULongLong() );
        settings->setValue("Range", (ui->bands->item(n,5) == NULL) ? 0.0 : ui->bands->item(n,5)->text().toDouble() );
    }
    settings->endArray();

    //settings->remove("Modes");
    settings->beginWriteArray("Modes");

    for (int n = 0; n<ui->modes->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num", (ui->modes->item(n,0) == NULL) ? 0 : ui->modes->item(n,0)->text().toInt());
        settings->setValue("Name",(ui->modes->item(n,1) == NULL) ? "" : ui->modes->item(n,1)->text());
        settings->setValue("BW",(ui->modes->item(n,2) == NULL) ? 0 : ui->modes->item(n,2)->text().toInt());
    }
    settings->endArray();

    //settings->remove("Attenuators");
    settings->beginWriteArray("Attenuators");
    for (int n = 0; n<ui->attenuators->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("dB",(ui->attenuators->item(n,0) == NULL) ? 0 :  ui->attenuators->item(n,0)->text().toInt());
    }
    settings->endArray();

    //settings->remove("Preamps");
    settings->beginWriteArray("Preamps");
    for (int n = 0; n<ui->preamps->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num",(ui->preamps->item(n,0) == NULL) ? 0 :  ui->preamps->item(n,0)->text().toInt());
        settings->setValue("Name",(ui->preamps->item(n,1) == NULL) ? "" :  ui->preamps->item(n,1)->text());
    }
    settings->endArray();

    //settings->remove("Antennas");
    settings->beginWriteArray("Antennas");
    for (int n = 0; n<ui->antennas->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num",(ui->antennas->item(n,0) == NULL) ? 0 :  ui->antennas->item(n,0)->text().toInt());
        settings->setValue("Name",(ui->antennas->item(n,1) == NULL) ? "" :  ui->antennas->item(n,1)->text());
    }
    settings->endArray();

    //settings->remove("Tuning Steps");
    settings->beginWriteArray("Tuning Steps");
    for (int n = 0; n<ui->tuningSteps->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num",(ui->tuningSteps->item(n,0) == NULL) ? 0 :  ui->tuningSteps->item(n,0)->text().toInt());
        settings->setValue("Hz",(ui->tuningSteps->item(n,1) == NULL) ? 0ULL :  ui->tuningSteps->item(n,1)->text().toULongLong());
    }
    settings->endArray();

    //settings->remove("Filters");
    settings->beginWriteArray("Filters");
    for (int n = 0; n<ui->filters->rowCount();n++)
    {
        settings->setArrayIndex(n);
        settings->setValue("Num",(ui->filters->item(n,0) == NULL) ? 0 :  ui->filters->item(n,0)->text().toInt());
        settings->setValue("Name",(ui->filters->item(n,1) == NULL) ? "" :  ui->filters->item(n,1)->text());
        settings->setValue("Modes",(ui->filters->item(n,2) == NULL) ? 0 : ui->filters->item(n,2)->text().toUInt(&ok,16));
    }
    settings->endArray();

    settings->endGroup();
    settings->sync();

    delete settings;
}




QStandardItemModel* rigCreator::createModel(QString strings[])
{

    commandsModel = new QStandardItemModel();

    for (int i=0; i < NUMFUNCS;i++)
    {
        QStandardItem *itemName = new QStandardItem(strings[i]);
        QStandardItem *itemId = new QStandardItem(i);

        QList<QStandardItem*> row;
        row << itemName << itemId;

        commandsModel->appendRow(row);
    }

    return commandsModel;
}

