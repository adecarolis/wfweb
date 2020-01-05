#include "wfmain.h"
#include <QApplication>




int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    //a.setStyle( "Fusion" );


    a.setOrganizationName("eliggett");
    a.setOrganizationDomain("nodomain");
    a.setApplicationName("wfview");

    a.setWheelScrollLines(1); // one line per wheel click
    wfmain w;
    w.show();

    return a.exec();
}
