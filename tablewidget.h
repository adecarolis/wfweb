
#ifndef TABLEWIDGET_H
#define TABLEWIDGET_H

#include <QTableWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QItemDelegate>
#include <QComboBox>
#include <QCheckBox>

class tableWidget : public QTableWidget
{
public:
    tableWidget(QWidget* parent = 0);

protected:
    void mouseReleaseEvent(QMouseEvent *event);
};


class tableCombobox : public QItemDelegate
{

public:
    explicit tableCombobox(QAbstractItemModel* model, bool sort=false, QObject *parent = 0);
    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *combo, const QModelIndex &index) const override;
    void setModelData(QWidget *combo, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *combo, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QAbstractItemModel* modelData;
};

class tableCheckbox : public QItemDelegate
{

public:
    explicit tableCheckbox(QObject *parent = 0);
    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *combo, const QModelIndex &index) const override;
    void setModelData(QWidget *combo, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *combo, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

#endif // TABLEWIDGET_H
