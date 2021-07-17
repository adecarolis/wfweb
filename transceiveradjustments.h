#ifndef TRANSCEIVERADJUSTMENTS_H
#define TRANSCEIVERADJUSTMENTS_H

#include <QWidget>

namespace Ui {
class transceiverAdjustments;
}

class transceiverAdjustments : public QWidget
{
    Q_OBJECT

public:
    explicit transceiverAdjustments(QWidget *parent = 0);
    ~transceiverAdjustments();

private:
    Ui::transceiverAdjustments *ui;
};

#endif // TRANSCEIVERADJUSTMENTS_H
