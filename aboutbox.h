#ifndef ABOUTBOX_H
#define ABOUTBOX_H

#include <QWidget>
#include <QDesktopServices>

namespace Ui {
class aboutbox;
}

class aboutbox : public QWidget
{
    Q_OBJECT

public:
    explicit aboutbox(QWidget *parent = 0);
    ~aboutbox();

private slots:
    void on_logoBtn_clicked();

private:
    Ui::aboutbox *ui;
};

#endif // ABOUTBOX_H
