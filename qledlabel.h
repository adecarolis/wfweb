#ifndef QLEDLABEL_H
#define QLEDLABEL_H

#include <QLabel>

class QLedLabel : public QLabel
{
    Q_OBJECT
public:
    explicit QLedLabel(QWidget* parent = 0);

    enum State {
        StateOk,
        StateOkBlue,
        StateWarning,
        StateError,
        StateSplitErrorOk,
        StateBlank
    };


signals:

public slots:
    void setState(State state);
    void setState(bool state);
    void setColor(QColor customColor, bool applyGradient);
    void setColor(QString colorString, bool applyGradient);
    void setColor(QColor c);
    void setColor(QString s);
    QColor getColor();

private:
    QColor baseColor;
};

#endif // QLEDLABEL_H
