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

signals:
    void requestSpectrumPeak(double peakFreq);
    void requestCurrentFreq(double tunedFreq);

private:
    Ui::calibrationWindow *ui;
};

#endif // CALIBRATIONWINDOW_H
