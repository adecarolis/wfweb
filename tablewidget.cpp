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
            int row=this->rowCount();
            this->insertRow(this->rowCount());
            emit rowAdded(row);
        }
        else if(selectedAction == insert)
        {
            int row=this->currentRow();
            this->insertRow(this->currentRow());
            emit rowAdded(row);
        }
        else if( selectedAction == clone )
        {
            int row=this->currentRow();
            this->insertRow(this->currentRow());
            for (int i=0;i<this->columnCount();i++)
            {
                if (this->item(row+1,i) != NULL) this->model()->setData(this->model()->index(row,i),this->item(row+1,i)->text());
            }
            emit rowAdded(row);
        }
        else if( selectedAction == del )
        {
            emit rowDeleted((this->item(this->currentRow(),1) == NULL) ? 0 : this->item(this->currentRow(),1)->text().toUInt());
            this->removeRow(this->currentRow());
        }
    }
}


tableEditor::tableEditor(QRegularExpression validExp, QObject *parent)
    : QItemDelegate(parent), validExp(validExp)
{
}

QWidget* tableEditor::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index)
    Q_UNUSED(option)
    edit = new QLineEdit(parent);
    if (validExp.isValid())
    {
        edit->setValidator(new QRegularExpressionValidator(validExp,edit));
    }
    edit->setFrame(false);
    return edit ;
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
    combo = new QComboBox(parent);
    QObject::connect(combo,SIGNAL(currentIndexChanged(int)),this,SLOT(setData(int)));
    combo->blockSignals(true);
    combo->setModel(modelData);
    combo->blockSignals(false);
    return combo;
}

void tableCombobox::setEditorData(QWidget *editor, const QModelIndex &index) const {
    // update model widget
    Q_UNUSED(editor)
    combo->blockSignals(true);
    QString text = index.model()->data( index, Qt::DisplayRole ).toString();
    combo->setCurrentIndex(combo->findText(text,Qt::MatchFixedString));
    combo->blockSignals(false);
}

void tableCombobox::setModelData(QWidget *editor, QAbstractItemModel *model,   const QModelIndex &index) const {
    Q_UNUSED(editor)
    model->setData( index, combo->currentText() );
}

void tableCombobox::updateEditorGeometry(QWidget *editor, const     QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index)
    editor->setGeometry(option.rect);
}

void tableCombobox::setData(int val)
{
    Q_UNUSED(val)

    if (combo != Q_NULLPTR) {
        emit commitData(combo);

    }
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
