
#include "tablewidget.h"



tableWidget::tableWidget(QWidget *parent): QTableWidget(parent)
{
}

void tableWidget::mouseReleaseEvent(QMouseEvent *event)
{
    QMenu menu;
    QAction *add= menu.addAction("Add Item");
    QAction *del = menu.addAction("Delete Item");
    if(event->button() == Qt::RightButton)
    {
        QAction *selectedAction = menu.exec(event->globalPosition().toPoint());

        if(selectedAction == add)
        {
            this->insertRow(this->rowCount());
        }
        else if( selectedAction == del )
        {
            this->removeRow(this->currentRow());
        }
    }
}


tableCombobox::tableCombobox(QAbstractItemModel* model, QObject *parent)
    : QItemDelegate(parent), modelData(model)
{

}

QWidget* tableCombobox::createEditor(QWidget *parent, const   QStyleOptionViewItem &option, const QModelIndex &index) const {
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
    editor->setGeometry(option.rect);
}



tableCheckbox::tableCheckbox(QObject *parent)
    : QItemDelegate(parent)
{

}

QWidget* tableCheckbox::createEditor(QWidget *parent, const   QStyleOptionViewItem &option, const QModelIndex &index) const {
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
    editor->setGeometry(option.rect);
}
