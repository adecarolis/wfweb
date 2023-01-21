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
}

cwSender::~cwSender()
{
    delete ui;
}

void cwSender::showEvent(QShowEvent *event)
{
    emit getCWSettings();
    (void)event;
}

void cwSender::handleKeySpeed(unsigned char wpm)
{
    if((wpm >= 6) && (wpm <=48))
    {
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

void cwSender::on_sendBtn_clicked()
{
    if( (ui->textToSendEdit->text().length() > 0) &&
        (ui->textToSendEdit->text().length() <= 30) )
    {
        emit sendCW(ui->textToSendEdit->text());
        ui->transcriptText->appendPlainText(ui->textToSendEdit->text());
        ui->textToSendEdit->clear();
        ui->textToSendEdit->setFocus();
    }
}

void cwSender::on_stopBtn_clicked()
{
    emit stopCW();
}

void cwSender::on_textToSendEdit_returnPressed()
{
    on_sendBtn_clicked();
}

void cwSender::on_breakinCombo_activated(int brkmode)
{
    // 0 = off, 1 = semi, 2 = full
    emit setBreakInMode((unsigned char)brkmode);
}

void cwSender::on_wpmSpin_valueChanged(int wpm)
{
    emit setKeySpeed((unsigned char)wpm);
}

void cwSender::on_macro1btn_clicked()
{
    processMacroButton(1);
}

void cwSender::on_macro2btn_clicked()
{
    processMacroButton(2);
}

void cwSender::on_macro3btn_clicked()
{
    processMacroButton(3);
}

void cwSender::on_macro4btn_clicked()
{
    processMacroButton(4);
}

void cwSender::on_macro5btn_clicked()
{
    processMacroButton(5);
}

void cwSender::on_macro6btn_clicked()
{
    processMacroButton(6);
}

void cwSender::on_macro7btn_clicked()
{
    processMacroButton(7);
}

void cwSender::on_macro8btn_clicked()
{
    processMacroButton(8);
}

void cwSender::on_macro9btn_clicked()
{
    processMacroButton(9);
}

void cwSender::on_macro10btn_clicked()
{
    processMacroButton(10);
}

void cwSender::processMacroButton(int buttonNumber)
{
    if(ui->macroEditChk->isChecked())
    {
        editMacroButton(buttonNumber);
    } else {
        runMacroButton(buttonNumber);
    }
}

void cwSender::runMacroButton(int buttonNumber)
{
    if(macroText[buttonNumber].isEmpty())
        return;
    emit sendCW(macroText[buttonNumber]);
    ui->transcriptText->appendPlainText(macroText[buttonNumber]);
    ui->textToSendEdit->setFocus();
}

void cwSender::editMacroButton(int buttonNumber)
{
    bool ok;
    QString prompt = QString("Please enter the text for macro %1, up to 30 characters.").arg(buttonNumber);
    QString newMacroText = QInputDialog::getText(this, "Macro Edit",
                          prompt,
    QLineEdit::Normal, macroText[buttonNumber], &ok);
    if(!ok)
        return;
    if(newMacroText.length() > 30)
        return;

    macroText[buttonNumber] = newMacroText;
}
