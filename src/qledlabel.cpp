#include "qledlabel.h"
#include <QDebug>

static const int SIZE = 16;
static const QString greenSS = QString("color: white;border-radius: %1;background-color: qlineargradient(spread:pad, x1:0.145, y1:0.16, x2:1, y2:1, stop:0 rgba(20, 252, 7, 255), stop:1 rgba(25, 134, 5, 255));").arg(SIZE / 2);
static const QString redSS = QString("color: white;border-radius: %1;background-color: qlineargradient(spread:pad, x1:0.145, y1:0.16, x2:0.92, y2:0.988636, stop:0 rgba(255, 12, 12, 255), stop:0.869347 rgba(103, 0, 0, 255));").arg(SIZE / 2);
static const QString rgSplitSS = QString("color: white;border-radius: %1;background-color: qlineargradient(spread:pad, x1:0.4, y1:0.5, x2:0.6, y2:0.5, stop:0 rgba(255, 0, 0, 255), stop:1.0 rgba(0, 255, 0, 255));").arg(SIZE / 2);
static const QString orangeSS = QString("color: white;border-radius: %1;background-color: qlineargradient(spread:pad, x1:0.232, y1:0.272, x2:0.98, y2:0.959773, stop:0 rgba(255, 113, 4, 255), stop:1 rgba(91, 41, 7, 255))").arg(SIZE / 2);
static const QString blueSS = QString("color: white;border-radius: %1;background-color: qlineargradient(spread:pad, x1:0.04, y1:0.0565909, x2:0.799, y2:0.795, stop:0 rgba(203, 220, 255, 255), stop:0.41206 rgba(0, 115, 255, 255), stop:1 rgba(0, 49, 109, 255));").arg(SIZE / 2);
static const QString blankSS = QString("color: white;border-radius: %1;background-color: qlineargradient(spread:pad, x1:0.04, y1:0.0565909, x2:0.799, y2:0.795, stop:0 rgba(203, 220, 255, 0), stop:0.41206 rgba(0, 115, 255, 0), stop:1 rgba(0, 49, 109, 0));").arg(SIZE / 2);

QLedLabel::QLedLabel(QWidget* parent) :
    QLabel(parent)
{
    //Set to ok by default
    setState(StateOkBlue);
    baseColor = QColor(0, 115, 255, 255);
    setFixedSize(SIZE, SIZE);
}

void QLedLabel::setState(State state)
{
    //Debug() << "LED: setState" << state;
    switch (state) {
    case StateOk:
        setStyleSheet(greenSS);
        break;
    case StateWarning:
        setStyleSheet(orangeSS);
        break;
    case StateError:
        setStyleSheet(redSS);
        break;
    case StateOkBlue:
        setStyleSheet(blueSS);
        break;
    case StateBlank:
        setStyleSheet(blankSS);
        break;
    case StateSplitErrorOk:
        setStyleSheet(rgSplitSS);
        break;
    default:
        setStyleSheet(blueSS);
        break;
    }
}

void QLedLabel::setState(bool state)
{
    setState(state ? StateOk : StateError);
}

void QLedLabel::setColor(QColor customColor, bool applyGradient=true)
{
    QColor top,middle,bottom;
    this->baseColor = customColor;

    if(applyGradient)
    {
        top = customColor.lighter(200);
        middle = customColor;
        bottom = customColor.darker(200);
    } else {
        top = customColor;
        middle = customColor;
        bottom = customColor;
    }

    // Stop 0 is the upper corner color, white-ish
    // Stop 0.4 is the middle color
    // Stop 1 is the bottom-right corner, darker.

    QString colorSS = QString("color: white;\
            border-radius: %1;background-color: \
            qlineargradient(spread:pad, x1:0.04, \
            y1:0.0565909, x2:0.799, y2:0.795, \
            stop:0 \
            rgba(%2, %3, %4, %5), \
            stop:0.41206 \
            rgba(%6, %7, %8, %9), \
            stop:1 \
            rgba(%10, %11, %12, %13));").arg(SIZE / 2)
            .arg(top.red()).arg(top.green()).arg(top.blue()).arg(top.alpha())
            .arg(middle.red()).arg(middle.green()).arg(middle.blue()).arg(middle.alpha())
            .arg(bottom.red()).arg(bottom.green()).arg(bottom.blue()).arg(bottom.alpha());

    setStyleSheet(colorSS);
}

void QLedLabel::setColor(QString colorString, bool applyGradient=true)
{
    QColor c = QColor(colorString);
    setColor(c, applyGradient);
}

void QLedLabel::setColor(QColor c)
{
    this->setColor(c, true);
}

void QLedLabel::setColor(QString s)
{
    this->setColor(s, true);
}

QColor QLedLabel::getColor()
{
    return baseColor;
}
