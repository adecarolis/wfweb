#include <QCoreApplication>
#include <QObject>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include <cstdio>
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
        int key = getchar();
        if (key == EOF) {
            // stdin is not an interactive terminal (daemon mode, systemd
            // Type=simple, or redirected/closed input). getchar() returns EOF
            // immediately and forever, so a read loop here would spin a CPU
            // core at 100%. There is no terminal to read keystrokes from, so
            // stop the thread.
            return;
        }
        if (key == 'q') {
            QCoreApplication::quit();
        }
    }
    return;
}