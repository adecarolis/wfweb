#include "ft4222handler.h"
#include "logcategories.h"


void ft4222Handler::run()
{
    if (!setup())
    {
        qInfo(logRig()) << "Failed to initialize FT4222 device";
        // Not sure why I get an exception if the thread quits before being asked?
        while (this->running)
        {
            this->msleep(500);
        }
        return;
    }

#ifdef FTDI_SUPPORT

    // If we got here, we are connected to a valid FT4222, so start processing


    QByteArray buf(4096,0x0);
    FT_STATUS status;
    uint16 size;

    QElapsedTimer timer;
    timer.start();

    quint64 currentPoll=20;
    while (this->running)
    {
        status = FT4222_SPIMaster_SingleRead(device,(uchar *)buf.data(), buf.size(), &size, false);

        if (FT_OK == status)
        {
            if (!buf.endsWith(QByteArrayLiteral("\xff\x01\xee\x01")))
            {
                sync();
                continue;
            }
        }

        if (timer.hasExpired(currentPoll))
        {

            emit dataReady(buf);
            timer.start();
            if (poll > currentPoll+10 || poll < currentPoll-10) {
                qDebug(logRig()) << "FT4222 Scope polling changed to" << poll << "from" << currentPoll << "ms";
                currentPoll = poll;
            }
        }
    }

    FT4222_UnInitialize(device);
    FT_Close(device);
#endif
}

void ft4222Handler::sync()
{
#ifdef FTDI_SUPPORT
    if (device != NULL)
    {
        uchar buf[16];
        uint16 size;
        int i=0;
        int b=0;
        while (b<8192)
        {
            if (FT4222_OK !=  FT4222_SPIMaster_SingleRead(device,&buf[i],1,&size,false) || size != 1)
            {
                qInfo(logRig()) << "FT4222 Error during sync!";
                return;
            }
            if(buf[0] != 0xff) // || buf[1] != 0x01 || buf[2] != 0xee || buf[3] != 0x01)
            {
                i=0;
            }
            else if (i == 15)
            {
                QByteArray sync = QByteArray((char *)buf,sizeof(buf));
                if (sync == QByteArrayLiteral("\xff\x01\xee\x01\xff\x01\xee\x01\xff\x01\xee\x01\xff\x01\xee\x01"))
                {
                    qInfo(logRig()) << "FT4222 re-synchronised";
                    return;
                }
                i=0;
            } else {
                ++i;
            }
            ++b;
        }
    }
    qInfo(logRig()) << "FT4222 failed to resync";
    setup();
#endif
}

bool ft4222Handler::setup()
{
#ifdef FTDI_SUPPORT
    FT_STATUS ftStatus;
    FT_STATUS ft4222_status;

    if (device != NULL)
    {
        FT4222_UnInitialize(device);
        FT_Close(device);
        device = NULL;
    }

    ftStatus = FT_OpenEx((void*) "FT4222 A", FT_OPEN_BY_DESCRIPTION, &device);
    if (FT_OK != ftStatus)
    {
        qInfo(logRig()) << "Could not open FT422 device";
        device = NULL;
        return false;
    }

    //Set default Read and Write timeout 100 msec
    ftStatus = FT_SetTimeouts(device, 100, 100 );
    if (FT_OK != ftStatus)
    {
        qInfo(logRig()) << "FT_SetTimeouts failed!";
        goto failed;
    }

    // set latency to 2, range is 2-255
    ftStatus = FT_SetLatencyTimer(device, 2);
    if (FT_OK != ftStatus)
    {
        qInfo(logRig()) << "FT_SetLatencyTimerfailed!";
        goto failed;
    }

    /*
    ftStatus = FT_SetUSBParameters(device, 4*1024, 0);
    if (FT_OK != ftStatus)
    {
        qInfo(logRig()) << "FT_SetUSBParameters failed!";
        goto failed;
    }
    */

    qInfo(logRig()) << "Init FT4222 as SPI master";
    ft4222_status = FT4222_SPIMaster_Init(device, SPI_IO_SINGLE, CLK_DIV_64, CLK_IDLE_HIGH, CLK_LEADING, 0x01);
    if (FT4222_OK != ft4222_status)
    {
        qInfo(logRig()) << "Init FT4222 as SPI master device failed!";
        goto failed;
    }

    ft4222_status = FT4222_SetClock(device,SYS_CLK_24);
    if (FT4222_OK != ft4222_status)
    {
        qInfo(logRig()) << "FT4222_SetClock() Failed to set clock to SYS_CLK_48";
        goto failed;
    }

    /*
    ft4222_status = FT4222_SPI_SetDrivingStrength(device,DS_8MA, DS_8MA, DS_8MA);
    if (FT4222_OK != ft4222_status)
    {
        qInfo(logRig()) << "FT4222_SPI_SetDrivingStrength failed!";
        goto failed;
    }
    */

    qInfo(logRig()) << "Init FT4222 as SPI master completed successfully!";


    return true;

failed:
    FT4222_UnInitialize(device);
    FT_Close(device);
    device = NULL;
    return false;
#else
    return false;
#endif

}
