#ifndef CALIBRATIONWINDOW_H
#define CALIBRATIONWINDOW_H

#include <QDialog>

namespace Ui {
class calibrationWindow;
}

class calibrationWindow : public QDialog
{
    Q_OBJECT

public:
    explicit calibrationWindow(QWidget *parent = 0);
    ~calibrationWindow();

public slots:
    void handleSpectrumPeak(double peakFreq);
    void handleCurrentFreq(double tunedFreq);
    void handleRefAdjustCourse(quint8);
    void handleRefAdjustFine(quint8);

signals:
    void requestSpectrumPeak(double peakFreq);
    void requestCurrentFreq(double tunedFreq);
    void requestRefAdjustCourse();
    void requestRefAdjustFine();
    void setRefAdjustCourse(quint8);
    void setRefAdjustFine(quint8);

private slots:
    void on_calReadRigCalBtn_clicked();

    void on_calCourseSlider_valueChanged(int value);

    void on_calFineSlider_valueChanged(int value);

    void on_calCourseSpinbox_valueChanged(int arg1);

    void on_calFineSpinbox_valueChanged(int arg1);

    void on_calFineSpinbox_editingFinished();

    void on_calCourseSpinbox_editingFinished();

private:
    Ui::calibrationWindow *ui;
};

#endif // CALIBRATIONWINDOW_H
