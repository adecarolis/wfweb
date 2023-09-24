#ifndef SCROLLTEST_H
#define SCROLLTEST_H

#include <QWidget>
#include <QPainter>
#include <QWheelEvent>
#include <QFont>
#include <QFontMetrics>

class scrolltest : public QWidget
{
    Q_OBJECT

    int widgetWindowHeight;
    int fontSize = 12;
    int rawClicksY;
    int rawClicksX;
    QString resultText = "--- SCROLL HERE ---";

    int maxX=0;
    int maxY=0;
    int minX=999999;
    int minY=999999;

public:
    explicit scrolltest(QWidget *parent = nullptr);

public slots:
    //void mouseMoveEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);
    void paintEvent(QPaintEvent *event);

signals:
    void haveRawClicksXY(int x, int y);
};

#endif // SCROLLTEST_H
