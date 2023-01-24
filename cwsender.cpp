#include "cwsender.h"
#include "ui_cwsender.h"

cwSender::cwSender(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::cwSender)
{
    ui->setupUi(this);
    this->setWindowTitle("CW Sender");
    ui->textToSendEdit->setFocus();
    QFont f = QFont("Monospace");
    f.setStyleHint(QFont::TypeWriter);
    ui->textToSendEdit->setFont(f);
    ui->transcriptText->setFont(f);
    ui->textToSendEdit->setFocus();
    ui->statusbar->setToolTipDuration(3000);
    this->setToolTipDuration(3000);
}

cwSender::~cwSender()
{
    qDebug(logCW()) << "Running CW Sender destructor.";
    delete ui;
}

void cwSender::showEvent(QShowEvent *event)
{
    emit getCWSettings();
    (void)event;
}

void cwSender::handleKeySpeed(unsigned char wpm)
{
    //qDebug(logCW()) << "Told that current WPM is" << wpm;
    if((wpm >= 6) && (wpm <=48))
    {
        //qDebug(logCW()) << "Setting WPM UI control to" << wpm;
        ui->wpmSpin->blockSignals(true);
        ui->wpmSpin->setValue(wpm);
        ui->wpmSpin->blockSignals(false);
    }
}

void cwSender::handleBreakInMode(unsigned char b)
{
    if(b < 3)
    {
        ui->breakinCombo->blockSignals(true);
        ui->breakinCombo->setCurrentIndex(b);
        ui->breakinCombo->blockSignals(false);
    }
}

void cwSender::handleCurrentModeUpdate(mode_kind mode)
{
    this->currentMode = mode;
    if( (currentMode==modeCW) || (currentMode==modeCW_R) )
    {
    } else {
        ui->statusbar->showMessage("Note: Mode needs to be set to CW or CW-R to send CW.", 3000);
    }
}

void cwSender::on_sendBtn_clicked()
{
    if( (ui->textToSendEdit->text().length() > 0) &&
        (ui->textToSendEdit->text().length() <= 30) )
    {
        emit sendCW(ui->textToSendEdit->text());
        ui->transcriptText->appendPlainText(ui->textToSendEdit->text());
        ui->textToSendEdit->clear();
        ui->textToSendEdit->setFocus();
        ui->statusbar->showMessage("Sending CW", 3000);
    }
    if( (currentMode==modeCW) || (currentMode==modeCW_R) )
    {
    } else {
        ui->statusbar->showMessage("Note: Mode needs to be set to CW or CW-R to send CW.", 3000);
    }
}

void cwSender::on_stopBtn_clicked()
{
    emit stopCW();
    ui->textToSendEdit->setFocus();
    ui->statusbar->showMessage("Stopping CW transmission.", 3000);
}

void cwSender::on_textToSendEdit_returnPressed()
{
    on_sendBtn_clicked();
}

void cwSender::on_breakinCombo_activated(int brkmode)
{
    // 0 = off, 1 = semi, 2 = full
    emit setBreakInMode((unsigned char)brkmode);
    ui->textToSendEdit->setFocus();
}

void cwSender::on_wpmSpin_valueChanged(int wpm)
{
    emit setKeySpeed((unsigned char)wpm);
}

void cwSender::on_macro1btn_clicked()
{
    processMacroButton(1, ui->macro1btn);
}

void cwSender::on_macro2btn_clicked()
{
    processMacroButton(2, ui->macro2btn);
}

void cwSender::on_macro3btn_clicked()
{
    processMacroButton(3, ui->macro3btn);
}

void cwSender::on_macro4btn_clicked()
{
    processMacroButton(4, ui->macro4btn);
}

void cwSender::on_macro5btn_clicked()
{
    processMacroButton(5, ui->macro5btn);
}

void cwSender::on_macro6btn_clicked()
{
    processMacroButton(6, ui->macro6btn);
}

void cwSender::on_macro7btn_clicked()
{
    processMacroButton(7, ui->macro7btn);
}

void cwSender::on_macro8btn_clicked()
{
    processMacroButton(8, ui->macro8btn);
}

void cwSender::on_macro9btn_clicked()
{
    processMacroButton(9, ui->macro9btn);
}

void cwSender::on_macro10btn_clicked()
{
    processMacroButton(10, ui->macro10btn);
}

void cwSender::processMacroButton(int buttonNumber, QPushButton *btn)
{
    if(ui->macroEditChk->isChecked())
    {
        editMacroButton(buttonNumber, btn);
    } else {
        runMacroButton(buttonNumber);
    }
    ui->textToSendEdit->setFocus();
}

void cwSender::runMacroButton(int buttonNumber)
{
    if(macroText[buttonNumber].isEmpty())
        return;
    QString outText;
    if(macroText[buttonNumber].contains("\%1"))
    {
        outText = macroText[buttonNumber].arg(sequenceNumber, 3, 10, QChar('0'));
        sequenceNumber++;
        ui->sequenceSpin->blockSignals(true);
        ui->sequenceSpin->setValue(sequenceNumber);
        ui->sequenceSpin->blockSignals(false);
    } else {
        outText = macroText[buttonNumber];
    }
    emit sendCW(outText);
    ui->transcriptText->appendPlainText(outText);
    ui->textToSendEdit->setFocus();

    if( (currentMode==modeCW) || (currentMode==modeCW_R) )
    {
        ui->statusbar->showMessage(QString("Sending CW macro %1").arg(buttonNumber));
    } else {
        ui->statusbar->showMessage("Note: Mode needs to be set to CW or CW-R to send CW.");
    }
}

void cwSender::editMacroButton(int buttonNumber, QPushButton* btn)
{
    bool ok;
    QString promptFirst = QString("Please enter the text for macro %1,\n"
                                  "up to 30 characters.\n").arg(buttonNumber);
    QString promptSecond = QString("You may use \"\%1\" to insert a sequence number.");
    QString prompt = promptFirst+promptSecond;

    QString newMacroText = QInputDialog::getText(this, "Macro Edit",
                                                 prompt,
                                                 QLineEdit::Normal, macroText[buttonNumber], &ok);
    if(!ok)
        return;

    if(newMacroText.length() > 30)
    {
        QMessageBox msgBox;
        msgBox.setText(QString("The text entered was too long \n"
                       "(max length is 30 characters).\n"
                       "Your input was %1 characters.").arg(newMacroText.length()));
        msgBox.exec();
        this->raise();
        return;
    }

    macroText[buttonNumber] = newMacroText;
    setMacroButtonText(newMacroText, btn);
}

void cwSender::setMacroButtonText(QString btnText, QPushButton *btn)
{
    if(btn==Q_NULLPTR)
        return;
    if(btnText.isEmpty())
        return;

    QString shortBtnName;
    if(btnText.length() <= 8)
    {
        shortBtnName = btnText;
    } else {
        shortBtnName = btnText.left(7);
        shortBtnName.append("…");
    }
    btn->setText(shortBtnName);
}

void cwSender::on_sequenceSpin_valueChanged(int newSeq)
{
    sequenceNumber = newSeq;
    ui->textToSendEdit->setFocus();
}

QStringList cwSender::getMacroText()
{
    // This is for preference saving:
    QStringList mlist;
    for(int i=1; i < 11; i++)
    {
        mlist << macroText[i];
    }
    return mlist;
}

void cwSender::setMacroText(QStringList macros)
{
    if(macros.length() != 10)
    {
        qCritical(logCW()) << "Macro list must be exactly 10. Rejecting macro text load.";
        return;
    }

    for(int i=0; i < 10; i++)
    {
        macroText[i+1] = macros.at(i);
    }

    setMacroButtonText(macroText[1], ui->macro1btn);
    setMacroButtonText(macroText[2], ui->macro2btn);
    setMacroButtonText(macroText[3], ui->macro3btn);
    setMacroButtonText(macroText[4], ui->macro4btn);
    setMacroButtonText(macroText[5], ui->macro5btn);
    setMacroButtonText(macroText[6], ui->macro6btn);
    setMacroButtonText(macroText[7], ui->macro7btn);
    setMacroButtonText(macroText[8], ui->macro8btn);
    setMacroButtonText(macroText[9], ui->macro9btn);
    setMacroButtonText(macroText[10], ui->macro10btn);
}
