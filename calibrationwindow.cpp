#include "calibrationwindow.h"
#include "ui_calibrationwindow.h"

calibrationWindow::calibrationWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::calibrationWindow)
{
    ui->setupUi(this);
}

calibrationWindow::~calibrationWindow()
{
    delete ui;
}

void calibrationWindow::handleCurrentFreq(double tunedFreq)
{

}

void calibrationWindow::handleSpectrumPeak(double peakFreq)
{

}
