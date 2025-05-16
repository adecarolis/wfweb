#include "ft4222handler.h"
#include "logcategories.h"


void ft4222Handler::run()
{
    findDevices();
    if (!devList.empty())
    {
        if (!setup(0))
        {
            qInfo(logRig()) << "Failed to initialize FT4222 device";
            return;
        }
    } else {
        qInfo(logRig()) << "No FT4222 devices found";
        return;
    }

    // If we got here, we are connected to a valid FT4222, so start processing


    ft710_spi_data buf;
    FT_STATUS status;
    uint16 size;

    QElapsedTimer timer;
    timer.start();

    while (this->running)
    {
        status = FT4222_SPIMaster_SingleRead(device,buf.packet, sizeof(buf.packet), &size, false);
        if (timer.hasExpired(30))
        {

            for (int i=0; i< sizeof(buf.wf1);i++) {
                buf.wf1[i] = ~buf.wf1[i];
            }
            QByteArray data = QByteArray((char*)buf.data,sizeof(buf.data));
            QByteArray sum = QByteArray((char*)buf.sync,sizeof(buf.sync));

            if (sum != QByteArrayLiteral("\xff\x01\xee\x01"))
            {
                sync();
                continue;
            }

            freqt fa,fb;
            quint16 model = data.mid(17,5).toHex().toUShort();
            fa.Hz = data.mid(64,5).toHex().toLongLong();
            fa.MHzDouble=fa.Hz/1000000.0;
            fb.Hz = data.mid(89,5).toHex().toLongLong();
            fb.MHzDouble=fb.Hz/1000000.0;
            quint8 byte32 = quint8(data.mid(32,1).toHex().toUShort());
            quint8 byte33 = quint8(data.mid(33,1).toHex().toUShort());

            quint8 mode = (byte32 >> 4);// & 0x0f;
            quint8 span = byte32 & 0x0f;
            quint8 speed = (byte33 >> 4);// & 0x0f;

            scopeData scope;
            scope.data = QByteArray((char*)buf.wf1,sizeof(buf.wf1));
            scope.valid=true;
            scope.receiver=0;
            scope.startFreq=fa.MHzDouble-0.002;
            scope.endFreq=fa.MHzDouble+0.002;
            scope.mode = mode;
            scope.oor = false;
            scope.fixedEdge = 1;

            emit dataReady(scope, fa, fb);
            timer.start();
        }
    }

    FT4222_UnInitialize(device);
    FT_Close(device);
}

void ft4222Handler::findDevices()
{
    /*
            qInfo(logRig()) << "Dev:" << iDev;
            qInfo(logRig()) << "Flags=" << devInfo.Flags;
            qInfo(logRig()) << "Type=" << devInfo.Type;
            qInfo(logRig()) << "ID=" << devInfo.ID;
            qInfo(logRig()) << "LocId=" << devInfo.LocId;
            qInfo(logRig()) << "SerialNumber=" << devInfo.SerialNumber;
            qInfo(logRig()) << "Description=" << devInfo.Description;
            qInfo(logRig()) << "ftHandle=" << devInfo.ftHandle;
    */

    FT_STATUS ftStatus = 0;

    DWORD numOfDevices = 0;
    ftStatus = FT_CreateDeviceInfoList(&numOfDevices);

    if (FT_OK == ftStatus)
    {
        for(DWORD iDev=0; iDev<numOfDevices; ++iDev)
        {
            FT_DEVICE_LIST_INFO_NODE devInfo;
            memset(&devInfo, 0, sizeof(devInfo));

            ftStatus = FT_GetDeviceInfoDetail(iDev, &devInfo.Flags, &devInfo.Type, &devInfo.ID, &devInfo.LocId,
                                              devInfo.SerialNumber,
                                              devInfo.Description,
                                              &devInfo.ftHandle);

            if (FT_OK == ftStatus)
            {
                const std::string desc = devInfo.Description;
                qInfo(logRig()) << "Found FT4222 device:" << devList.size() << desc;
                devList.push_back(devInfo);
            }
        }
    }

    return;
}

void ft4222Handler::sync()
{
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
    setup(0);
}

bool ft4222Handler::setup(uchar num)
{
    FT_STATUS ftStatus;
    FT_STATUS ft4222_status;

    if (device != NULL)
    {
        FT4222_UnInitialize(device);
        FT_Close(device);
        device = NULL;
    }

    ftStatus = FT_Open(num, &device);
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

    ftStatus = FT_SetUSBParameters(device, 4*1024, 0);
    if (FT_OK != ftStatus)
    {
        qInfo(logRig()) << "FT_SetUSBParameters failed!";
        goto failed;
    }

    qInfo(logRig()) << "Init FT4222 as SPI master";
    ft4222_status = FT4222_SPIMaster_Init(device, SPI_IO_SINGLE, CLK_DIV_64, CLK_IDLE_HIGH, CLK_TRAILING, 0x01);
    if (FT4222_OK != ft4222_status)
    {
        qInfo(logRig()) << "Init FT4222 as SPI master device failed!";
        goto failed;
    }

    ft4222_status = FT4222_SetClock(device,SYS_CLK_48);
    if (FT4222_OK != ft4222_status)
    {
        qInfo(logRig()) << "FT4222_SetClock() Failed to set clock to SYS_CLK_48";
        goto failed;
    }

    //ft4222_status = FT4222_SPI_SetDrivingStrength(device,DS_8MA, DS_8MA, DS_8MA);
    //if (FT4222_OK != ft4222_status)
    //{
    //    qInfo(logRig()) << "FT4222_SPI_SetDrivingStrength failed!";
    //    goto failed;
    //}

    qInfo(logRig()) << "Init FT4222 as SPI master completed successfully!";


    return true;

failed:
    FT4222_UnInitialize(device);
    FT_Close(device);
    device = NULL;
    return false;
}
