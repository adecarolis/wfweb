#include "frequencyinputwidget.h"
#include "ui_frequencyinputwidget.h"

frequencyinputwidget::frequencyinputwidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::frequencyinputwidget)
{
    ui->setupUi(this);
    this->setWindowTitle("Frequency Input");
    ui->freqMhzLineEdit->setValidator( new QDoubleValidator(0, 100, 6, this));
    this->setObjectName("freq Input");
    queue = cachingQueue::getInstance();
}

frequencyinputwidget::~frequencyinputwidget()
{
    delete ui;
}

void frequencyinputwidget::showEvent(QShowEvent *event)
{
    ui->freqMhzLineEdit->setFocus();
    QWidget::showEvent(event);
}

void frequencyinputwidget::setAutomaticSidebandSwitching(bool autossb)
{
    this->automaticSidebandSwitching = autossb;
}

void frequencyinputwidget::updateCurrentMode(rigMode_t mode)
{
    currentMode = mode;
}

void frequencyinputwidget::updateFilterSelection(int filter)
{
    currentFilter = filter;
}

void frequencyinputwidget::on_f1btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("1"));
}

void frequencyinputwidget::on_f2btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("2"));
}

void frequencyinputwidget::on_f3btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("3"));
}

void frequencyinputwidget::on_f4btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("4"));
}

void frequencyinputwidget::on_f5btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("5"));
}

void frequencyinputwidget::on_f6btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("6"));
}

void frequencyinputwidget::on_f7btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("7"));
}

void frequencyinputwidget::on_f8btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("8"));
}

void frequencyinputwidget::on_f9btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("9"));
}

void frequencyinputwidget::on_fDotbtn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("."));
}

void frequencyinputwidget::on_f0btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("0"));
}

void frequencyinputwidget::on_fCEbtn_clicked()
{
    ui->freqMhzLineEdit->clear();
    freqTextSelected = false;
}

void frequencyinputwidget::on_fStoBtn_clicked()
{
    // Memory Store

    // sequence:
    // type frequency
    // press Enter or Go
    // change mode if desired
    // type in index number 0 through 99
    // press STO

    bool ok;
    int preset_number = ui->freqMhzLineEdit->text().toInt(&ok);

    if(ok && (preset_number >= 0) && (preset_number < 100))
    {
        //emit setMemory(preset_number, freq, currentMode);
        emit saveMemoryPreset(preset_number);
        //mem.setPreset(preset_number, freq.MHzDouble, (rigMode_t)ui->modeSelectCombo->currentData().toInt() );
        //showStatusBarText( QString("Storing frequency %1 to memory location %2").arg( freq.MHzDouble ).arg(preset_number) );
    } else {
        //showStatusBarText(QString("Could not store preset to %1. Valid preset numbers are 0 to 99").arg(preset_number));
    }
    ui->freqMhzLineEdit->clear();
}

void frequencyinputwidget::on_fRclBtn_clicked()
{
    // Memory Recall
    bool ok;
    int preset_number = ui->freqMhzLineEdit->text().toInt(&ok);

    if(ok && (preset_number >= 0) && (preset_number < 100))
    {
        emit gotoMemoryPreset(preset_number);
        //        temp = mem.getPreset(preset_number);
        //        // TODO: change to int hz
        //        // TODO: store filter setting as well.
        //        freqString = QString("%1").arg(temp.frequency);
        //        ui->freqMhzLineEdit->setText( freqString );
        //        ui->goFreqBtn->click();
        //        setModeVal = temp.mode;
        //        setFilterVal = ui->modeFilterCombo->currentIndex()+1; // TODO, add to memory
        //        issueDelayedCommand(cmdSetModeFilter);
//        issueDelayedCommand(cmdGetMode);
    } else {
        qInfo(logSystem()) << "Could not recall preset. Valid presets are 0 through 99.";
    }
    ui->freqMhzLineEdit->clear();
}

void frequencyinputwidget::on_fEnterBtn_clicked()
{
    on_goFreqBtn_clicked();
}

void frequencyinputwidget::on_fBackbtn_clicked()
{
    QString currentFreq = ui->freqMhzLineEdit->text();
    currentFreq.chop(1);
    ui->freqMhzLineEdit->setText(currentFreq);
}

void frequencyinputwidget::on_goFreqBtn_clicked()
{
    freqt f;
    bool ok = false;
    double freqDbl = 0;
    int KHz = 0;

    if(ui->freqMhzLineEdit->text().contains("."))
    {

        freqDbl = ui->freqMhzLineEdit->text().toDouble(&ok);
        if(ok)
        {
            f.Hz = freqDbl*1E6;
        }
    } else {
        KHz = ui->freqMhzLineEdit->text().toInt(&ok);
        if(ok)
        {
            f.Hz = KHz*1E3;
        }
    }
    if(ok)
    {
        modeInfo m;
        m.mk = sidebandChooser::getMode(f, currentMode);
        m.reg = (quint8) m.mk;
        m.filter = currentFilter;
        // TODO: usingDataMode
        // TODO: auto sideband preference
        if((m.mk != currentMode) && !usingDataMode && automaticSidebandSwitching)
        {
            queue->add(priorityImmediate,queueItem(funcMainMode,QVariant::fromValue<modeInfo>(m),false,false));
            currentMode = m.mk;
        }

        f.MHzDouble = (float)f.Hz / 1E6;
        //emit updateUIFrequency(f);
        currentFrequency = f;
        queue->add(priorityImmediate,queueItem(funcMainFreq,QVariant::fromValue<freqt>(f),false,false));
    } else {
        qWarning(logGui()) << "Could not understand frequency" << ui->freqMhzLineEdit->text();
        ui->freqMhzLineEdit->clear();
    }

    //ui->freqMhzLineEdit->clear();
    ui->freqMhzLineEdit->selectAll();
    freqTextSelected = true;
    //ui->tabWidget->setCurrentIndex(0);
}

void frequencyinputwidget::on_freqMhzLineEdit_returnPressed()
{
    on_goFreqBtn_clicked();
}

void frequencyinputwidget::checkFreqSel()
{
    if(freqTextSelected)
    {
        freqTextSelected = false;
        ui->freqMhzLineEdit->clear();
    }
}


