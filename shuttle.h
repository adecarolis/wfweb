#ifndef SHUTTLE_H
#define SHUTTLE_H

#include <iostream>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>

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

    void button0(bool);
    void button1(bool);
    void button2(bool);
    void button3(bool);
    void button4(bool);
    void button5(bool);
    void button6(bool);
    void button7(bool);
    void button8(bool);
    void button9(bool);
    void button10(bool);
    void button11(bool);
    void button12(bool);
    void button13(bool);
    void button14(bool);
    void button15(bool);

private:
    hid_device* handle;
    bool isOpen=false;
    unsigned int buttons=0;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    enum { NONE, shuttleXpress, shuttlePro2, RC28 }usbDevice;

protected:
};


#endif
