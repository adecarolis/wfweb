#include "transceiveradjustments.h"
#include "ui_transceiveradjustments.h"

transceiverAdjustments::transceiverAdjustments(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::transceiverAdjustments)
{
    ui->setupUi(this);
    // request level updates

}

transceiverAdjustments::~transceiverAdjustments()
{
    delete ui;
}
