#include "calibrationwindow.h"
#include "ui_calibrationwindow.h"
#include "logcategories.h"

calibrationWindow::calibrationWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::calibrationWindow)
{
    ui->setupUi(this);
    ui->calCourseSlider->setDisabled(true);
    ui->calCourseSpinbox->setDisabled(true);

    ui->calFineSlider->setDisabled(true);
    ui->calFineSpinbox->setDisabled(true);

}

calibrationWindow::~calibrationWindow()
{
    delete ui;
}

void calibrationWindow::handleCurrentFreq(double tunedFreq)
{
    (void)tunedFreq;
}

void calibrationWindow::handleSpectrumPeak(double peakFreq)
{
    (void)peakFreq;
}

void calibrationWindow::handleRefAdjustCourse(unsigned char value)
{
    ui->calCourseSlider->setDisabled(false);
    ui->calCourseSpinbox->setDisabled(false);

    ui->calCourseSlider->blockSignals(true);
    ui->calCourseSpinbox->blockSignals(true);

    ui->calCourseSlider->setValue((int) value);
    ui->calCourseSpinbox->setValue((int) value);

    ui->calCourseSlider->blockSignals(false);
    ui->calCourseSpinbox->blockSignals(false);
}

void calibrationWindow::handleRefAdjustFine(unsigned char value)
{
    ui->calFineSlider->setDisabled(false);
    ui->calFineSpinbox->setDisabled(false);

    ui->calFineSlider->blockSignals(true);
    ui->calFineSpinbox->blockSignals(true);

    ui->calFineSlider->setValue((int) value);
    ui->calFineSpinbox->setValue((int) value);

    ui->calFineSlider->blockSignals(false);
    ui->calFineSpinbox->blockSignals(false);
}

void calibrationWindow::on_calReadRigCalBtn_clicked()
{
    emit requestRefAdjustCourse();
    emit requestRefAdjustFine();
}

void calibrationWindow::on_calCourseSlider_valueChanged(int value)
{
    ui->calCourseSpinbox->blockSignals(true);
    ui->calCourseSpinbox->setValue((int) value);
    ui->calCourseSpinbox->blockSignals(false);

    emit setRefAdjustCourse((unsigned char) value);

}

void calibrationWindow::on_calFineSlider_valueChanged(int value)
{
    ui->calFineSpinbox->blockSignals(true);
    ui->calFineSpinbox->setValue((int) value);
    ui->calFineSpinbox->blockSignals(false);

    emit setRefAdjustFine((unsigned char) value);
}

void calibrationWindow::on_calCourseSpinbox_valueChanged(int value)
{
    // this one works with the up and down arrows,
    // however, if typing in a value, say "128",
    // this will get called three times with these values:
    // 1
    // 12
    // 128

    //int value = ui->calFineSpinbox->value();

    ui->calCourseSlider->blockSignals(true);
    ui->calCourseSlider->setValue(value);
    ui->calCourseSlider->blockSignals(false);

    emit setRefAdjustCourse((unsigned char) value);


}

void calibrationWindow::on_calFineSpinbox_valueChanged(int value)
{
    //int value = ui->calFineSpinbox->value();

    ui->calFineSlider->blockSignals(true);
    ui->calFineSlider->setValue(value);
    ui->calFineSlider->blockSignals(false);

    emit setRefAdjustFine((unsigned char) value);
}

void calibrationWindow::on_calFineSpinbox_editingFinished()
{

}

void calibrationWindow::on_calCourseSpinbox_editingFinished()
{
    // This function works well for typing in values
    // but the up and down arrows on the spinbox will not
    // trigger this function, until the enter key is pressed.

}
