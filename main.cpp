#include "wfmain.h"
#include <QApplication>




int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    wfmain w;
    w.show();

    return a.exec();
}
