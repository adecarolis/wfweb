#ifndef SHUTTLE_H
#define SHUTTLE_H

#include <iostream>
#include <qthread >
#include <QCoreApplication>
#include <QTimer>
#include "hidapi.h"

#ifndef Q_OS_WIN
//Headers needed for sleeping.
#include <unistd.h>
#endif

using namespace std;

#define HIDDATALENGTH 5

class shuttle : public QObject
{
    Q_OBJECT

public:
    shuttle();
    ~shuttle();
    int hidApiWrite(unsigned char* data, unsigned char length);

public slots:
    void init();
    void run();
    void runTimer();

signals:
    void hidDataArrived(QByteArray data);

private:
    hid_device* handle;
    bool isOpen=false;
    unsigned char buttons;
    unsigned char jogpos;
    unsigned char shutpos;

protected:
};


#endif