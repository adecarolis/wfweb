#ifndef METER_H
#define METER_H

#include <QWidget>
#include <QPainter>

class meter : public QWidget
{
    Q_OBJECT
public:
    explicit meter(QWidget *parent = nullptr);

signals:

public slots:
    void paintEvent(QPaintEvent *);

    void updateDrawing(int num);
    void setLevels(int current, int peak, int average);


private:
    //QPainter painter;
    int fontSize = 5;
    int length=30;
    int current=0;
    int peak = 0;
    int average = 0;

    int mstart = 10; // Starting point for S=0.
    int mheight = 14; // "thickness" of the meter block rectangle

    void drawScale(QPainter *qp);

    QColor currentColor;
    QColor averageColor;
    QColor peakColor;
    // S0-S9:
    QColor lowTextColor;
    QColor lowLineColor;
    // S9+:
    QColor highTextColor;
    QColor highLineColor;

};

#endif // METER_H
