#ifndef REPEATERSETUP_H
#define REPEATERSETUP_H

#include <QMainWindow>

#include "repeaterattributes.h"

namespace Ui {
class repeaterSetup;
}

class repeaterSetup : public QMainWindow
{
    Q_OBJECT

public:
    explicit repeaterSetup(QWidget *parent = 0);
    ~repeaterSetup();

private:
    Ui::repeaterSetup *ui;
};

#endif // REPEATERSETUP_H
