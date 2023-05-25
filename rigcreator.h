#ifndef RIGCREATOR_H
#define RIGCREATOR_H

#include <QDialog>
#include <QItemDelegate>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
#include <QHeaderView>
#include <QStandardPaths>
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
    void on_hasCommand29_toggled(bool checked);
    void on_defaultRigs_clicked(bool clicked);
    void loadRigFile(QString filename);
    void saveRigFile(QString filename);
    void commandRowAdded(int row);


private:
    Ui::rigCreator *ui;
    QMenu* context;
    tableCombobox* commandsList;
    QStandardItemModel* commandsModel;
    QStandardItemModel* command36Model;
    QStandardItemModel* createModel(QStandardItemModel* model, QString strings[]);
    QString currentFile;

};



#endif // RIGCREATOR_H
