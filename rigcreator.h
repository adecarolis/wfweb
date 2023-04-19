#ifndef RIGCREATOR_H
#define RIGCREATOR_H

#include <QDialog>
#include <QItemDelegate>
#include <QStandardItem>
#include <QStandardItemModel>
#include "wfviewtypes.h"
#include "rigidentities.h"
#include "tablewidget.h"


namespace Ui {
class rigCreator;
}

class rigCreator : public QDialog
{
    Q_OBJECT

public:
    explicit rigCreator(QWidget *parent = nullptr);
    ~rigCreator();

private slots:
    void on_loadFile_clicked(bool clicked);
    void on_saveFile_clicked(bool clicked);

private:
    Ui::rigCreator *ui;
    QMenu* context;
    tableCombobox* commandsList;
    QStandardItemModel* commandsModel;
    QStandardItemModel* createModel(QString strings[]);

};



#endif // RIGCREATOR_H
