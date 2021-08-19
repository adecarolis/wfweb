#ifndef SHUTTLE_H
#define SHUTTLE_H

#include <iostream>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>

#ifndef Q_OS_WIN
#include "hidapi/hidapi.h"
#else
#include "hidapi.h"
#endif

#ifndef Q_OS_WIN
//Headers needed for sleeping.
#include <unistd.h>
#endif

using namespace std;


#define HIDDATALENGTH 64
#define MAX_STR 255

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
    void jogPlus();
    void jogMinus();

    void doShuttle(bool plus, quint8 level);

    void button(bool,unsigned char num);

private:
    hid_device* handle;
    bool isOpen=false;
    unsigned int buttons=0;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    enum { NONE, shuttleXpress, shuttlePro2, RC28 }usbDevice;
    QTime	lastShuttle = QTime::currentTime();


protected:
};


#endif
