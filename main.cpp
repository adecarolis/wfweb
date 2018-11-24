#include "wfmain.h"
#include <QApplication>




int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    //a.setStyle( "Fusion" );


    a.setOrganizationName("liggett");
    a.setOrganizationDomain("nodomain");
    a.setApplicationName("RigView");

    wfmain w;
    w.show();

    return a.exec();
}
