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
