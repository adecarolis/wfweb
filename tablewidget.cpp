#include <QDebug>
#include "logcategories.h"

#include "tablewidget.h"



tableWidget::tableWidget(QWidget *parent): QTableWidget(parent)
{
}

void tableWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button() == Qt::RightButton)
    {
        QMenu menu;
        QAction *add= menu.addAction("Add Item");
        QAction *insert= menu.addAction("Insert Item");
        QAction *clone= menu.addAction("Clone Item");
        QAction *del = menu.addAction("Delete Item");
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        QAction *selectedAction = menu.exec(event->globalPos());
#else
        QAction *selectedAction = menu.exec(event->globalPosition().toPoint());
#endif


        if(selectedAction == add)
        {
            this->insertRow(this->rowCount());
        }
        else if(selectedAction == insert)
        {
            this->insertRow(this->currentRow());
        }
        else if( selectedAction == clone )
        {
            this->insertRow(this->currentRow());
            for (int i=0;i<this->columnCount();i++)
            {
                if (this->item(currentRow(),i) != NULL) this->model()->setData(this->model()->index(this->currentRow()-1,i),this->item(this->currentRow(),i)->text());
            }
        }
        else if( selectedAction == del )
        {
            this->removeRow(this->currentRow());
        }
    }
}


tableCombobox::tableCombobox(QAbstractItemModel* model, bool sort, QObject *parent)
    : QItemDelegate(parent), modelData(model)
{
    if (sort)
        modelData->sort(0);
}

QWidget* tableCombobox::createEditor(QWidget *parent, const   QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index)
    Q_UNUSED(option)
    QComboBox* comboBox = new QComboBox(parent);
    comboBox->setModel(modelData);    
    return comboBox;
}

void tableCombobox::setEditorData(QWidget *editor, const QModelIndex &index) const {
    // update model widget
    QString value = index.model()->data(index, Qt::EditRole).toString();
    qDebug() << "Value:" << value;
    QComboBox* comboBox = static_cast<QComboBox*>(editor);
    comboBox->setCurrentIndex(comboBox->findText(value));
}

void tableCombobox::setModelData(QWidget *editor, QAbstractItemModel *model,   const QModelIndex &index) const {
    // store edited model data to model
    QComboBox* comboBox = static_cast<QComboBox*>(editor);
    QString value = comboBox->currentText();
    model->setData(index, value, Qt::EditRole);
}

void tableCombobox::updateEditorGeometry(QWidget *editor, const     QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index)
    editor->setGeometry(option.rect);
}



tableCheckbox::tableCheckbox(QObject *parent)
    : QItemDelegate(parent)
{

}

QWidget* tableCheckbox::createEditor(QWidget *parent, const   QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index)
    Q_UNUSED(option)
    QCheckBox* checkBox = new QCheckBox(parent);
    return checkBox;
}

void tableCheckbox::setEditorData(QWidget *editor, const QModelIndex &index) const {
    // update model widget
    bool value = index.model()->data(index, Qt::EditRole).toBool();
    qDebug() << "Value:" << value;
    QCheckBox* checkBox = static_cast<QCheckBox*>(editor);
    checkBox->setEnabled(value);
}

void tableCheckbox::setModelData(QWidget *editor, QAbstractItemModel *model,   const QModelIndex &index) const {
    // store edited model data to model
    QCheckBox* checkBox = static_cast<QCheckBox*>(editor);
    bool value = checkBox->isChecked();
    model->setData(index, value, Qt::EditRole);
}

void tableCheckbox::updateEditorGeometry(QWidget *editor, const     QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index)
    editor->setGeometry(option.rect);
}
