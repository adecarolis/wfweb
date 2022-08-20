#include <QCoreApplication>
#include <QObject>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include "keyboard.h"

keyboard::keyboard(void)
{
}

keyboard::~keyboard(void)
{
}

void keyboard::run()
{
    while (true)
    {
        char key = getchar();
        if (key == 'q') {
            QCoreApplication::quit();
        }
    }
    return;
}