#ifndef SATELLITESETUP_H
#define SATELLITESETUP_H

#include <QDialog>

namespace Ui {
class satelliteSetup;
}

class satelliteSetup : public QDialog
{
    Q_OBJECT

public:
    explicit satelliteSetup(QWidget *parent = 0);
    ~satelliteSetup();

private:
    Ui::satelliteSetup *ui;
};

#endif // SATELLITESETUP_H
