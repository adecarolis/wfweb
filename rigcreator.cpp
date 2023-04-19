#include "rigcreator.h"
#include "ui_rigcreator.h"

rigCreator::rigCreator(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::rigCreator)
{
    ui->setupUi(this);
    commandsList = new tableCombobox(createModel(funcString));
    ui->commands->setItemDelegateForColumn(0, commandsList);

}

rigCreator::~rigCreator()
{
    delete ui;
}


QStandardItemModel* rigCreator::createModel(QString strings[])
{

    commandsModel = new QStandardItemModel();

    int i=0;
    for (int i=0; i < NUMFUNCS;i++)
    {
        QStandardItem *itemName = new QStandardItem(strings[i]);
        QStandardItem *itemId = new QStandardItem(i);

        QList<QStandardItem*> row;
        row << itemName << itemId;

        commandsModel->appendRow(row);
    }

    return commandsModel;
}
