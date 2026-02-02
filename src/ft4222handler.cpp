#include "ft4222handler.h"
#include "logcategories.h"

ft4222Handler::ft4222Handler()
{
    ftdiLoaded = loadFTDILibraries();
    if (!ftdiLoaded)
    {
        qInfo(logRig()) << "FTDI libraries not available - FT4222 support disabled";
    }
}

ft4222Handler::~ft4222Handler()
{
    unloadFTDILibraries();
}

bool ft4222Handler::loadFTDILibraries()
{
#ifdef Q_OS_LINUX
    // On Linux, libft4222 contains both FTD2XX and FT4222 functions
    // Try shared library first (.so)
    ft4222Lib.setFileName("libft4222");

    if (!ft4222Lib.load())
    {
        // Try with version number
        ft4222Lib.setFileName("libft4222.so.1.4.4.44");
    }

    if (!ft4222Lib.load())
    {
        qInfo(logRig()) << "Failed to load ft4222 library:" << ft4222Lib.errorString();
        return false;
    }

    // On Linux, resolve functions from the single library
    FT_OpenEx = (FT_OpenExFunc)ft4222Lib.resolve("FT_OpenEx");
    FT_Close = (FT_CloseFunc)ft4222Lib.resolve("FT_Close");
    FT_SetTimeouts = (FT_SetTimeoutsFunc)ft4222Lib.resolve("FT_SetTimeouts");
    FT_SetLatencyTimer = (FT_SetLatencyTimerFunc)ft4222Lib.resolve("FT_SetLatencyTimer");
    FT4222_UnInitialize = (FT4222_UnInitializeFunc)ft4222Lib.resolve("FT4222_UnInitialize");
    FT4222_SPIMaster_Init = (FT4222_SPIMaster_InitFunc)ft4222Lib.resolve("FT4222_SPIMaster_Init");
    FT4222_SPIMaster_SingleRead = (FT4222_SPIMaster_SingleReadFunc)ft4222Lib.resolve("FT4222_SPIMaster_SingleRead");
    FT4222_SetClock = (FT4222_SetClockFunc)ft4222Lib.resolve("FT4222_SetClock");

#elif defined(Q_OS_MACOS)
    // macOS: Documentation is contradictory - try both approaches
    // First try libft4222 alone (newer versions may have D2XX built-in)
    ft4222Lib.setFileName("libft4222");

    if (ft4222Lib.load())
    {
        // Try to resolve D2XX functions from ft4222 library (if built-in)
        FT_OpenEx = (FT_OpenExFunc)ft4222Lib.resolve("FT_OpenEx");

        if (FT_OpenEx != nullptr)
        {
            // D2XX is built into libft4222, resolve all from one library
            qInfo(logRig()) << "Using combined libft4222 (with D2XX built-in)";
            FT_Close = (FT_CloseFunc)ft4222Lib.resolve("FT_Close");
            FT_SetTimeouts = (FT_SetTimeoutsFunc)ft4222Lib.resolve("FT_SetTimeouts");
            FT_SetLatencyTimer = (FT_SetLatencyTimerFunc)ft4222Lib.resolve("FT_SetLatencyTimer");
        }
        else
        {
            // D2XX not built-in, need separate library
            qInfo(logRig()) << "libft4222 loaded, attempting to load separate libftd2xx";
            ftd2xxLib.setFileName("libftd2xx");

            if (!ftd2xxLib.load())
            {
                qInfo(logRig()) << "Failed to load libftd2xx:" << ftd2xxLib.errorString();
                ft4222Lib.unload();
                return false;
            }

            // Resolve D2XX functions from separate library
            FT_OpenEx = (FT_OpenExFunc)ftd2xxLib.resolve("FT_OpenEx");
            FT_Close = (FT_CloseFunc)ftd2xxLib.resolve("FT_Close");
            FT_SetTimeouts = (FT_SetTimeoutsFunc)ftd2xxLib.resolve("FT_SetTimeouts");
            FT_SetLatencyTimer = (FT_SetLatencyTimerFunc)ftd2xxLib.resolve("FT_SetLatencyTimer");
        }

        // Resolve FT4222 functions
        FT4222_UnInitialize = (FT4222_UnInitializeFunc)ft4222Lib.resolve("FT4222_UnInitialize");
        FT4222_SPIMaster_Init = (FT4222_SPIMaster_InitFunc)ft4222Lib.resolve("FT4222_SPIMaster_Init");
        FT4222_SPIMaster_SingleRead = (FT4222_SPIMaster_SingleReadFunc)ft4222Lib.resolve("FT4222_SPIMaster_SingleRead");
        FT4222_SetClock = (FT4222_SetClockFunc)ft4222Lib.resolve("FT4222_SetClock");
    }
    else
    {
        qInfo(logRig()) << "Failed to load libft4222:" << ft4222Lib.errorString();
        return false;
    }

#else // Windows
    // Windows uses separate libraries
    ftd2xxLib.setFileName("ftd2xx");

    if (!ftd2xxLib.load())
    {
        qInfo(logRig()) << "Failed to load ftd2xx library:" << ftd2xxLib.errorString();
        return false;
    }

// Load the FT4222 library - handle x86/x64 differences
#if defined(Q_PROCESSOR_X86_64) || defined(_WIN64)
    ft4222Lib.setFileName("LibFT4222-64");
#else
    ft4222Lib.setFileName("LibFT4222");
#endif

    if (!ft4222Lib.load())
    {
        // If the architecture-specific one failed, try the other
        qInfo(logRig()) << "Failed to load" << ft4222Lib.fileName() << "- trying alternative";
#if defined(Q_PROCESSOR_X86_64) || defined(_WIN64)
        ft4222Lib.setFileName("LibFT4222");
#else
        ft4222Lib.setFileName("LibFT4222-64");
#endif

        if (!ft4222Lib.load())
        {
            qInfo(logRig()) << "Failed to load ft4222 library:" << ft4222Lib.errorString();
            ftd2xxLib.unload();
            return false;
        }
    }

    // Resolve FTD2XX functions from ftd2xx library
    FT_OpenEx = (FT_OpenExFunc)ftd2xxLib.resolve("FT_OpenEx");
    FT_Close = (FT_CloseFunc)ftd2xxLib.resolve("FT_Close");
    FT_SetTimeouts = (FT_SetTimeoutsFunc)ftd2xxLib.resolve("FT_SetTimeouts");
    FT_SetLatencyTimer = (FT_SetLatencyTimerFunc)ftd2xxLib.resolve("FT_SetLatencyTimer");

    // Resolve FT4222 functions from ft4222 library
    FT4222_UnInitialize = (FT4222_UnInitializeFunc)ft4222Lib.resolve("FT4222_UnInitialize");
    FT4222_SPIMaster_Init = (FT4222_SPIMaster_InitFunc)ft4222Lib.resolve("FT4222_SPIMaster_Init");
    FT4222_SPIMaster_SingleRead = (FT4222_SPIMaster_SingleReadFunc)ft4222Lib.resolve("FT4222_SPIMaster_SingleRead");
    FT4222_SetClock = (FT4222_SetClockFunc)ft4222Lib.resolve("FT4222_SetClock");
#endif
    // Check if all functions were resolved
    if (!FT_OpenEx || !FT_Close || !FT_SetTimeouts || !FT_SetLatencyTimer ||
        !FT4222_UnInitialize || !FT4222_SPIMaster_Init ||
        !FT4222_SPIMaster_SingleRead || !FT4222_SetClock)
    {
        qInfo(logRig()) << "Failed to resolve all FTDI functions";
        unloadFTDILibraries();
        return false;
    }

    qInfo(logRig()) << "FTDI libraries loaded successfully";
    return true;
}

void ft4222Handler::unloadFTDILibraries()
{
    if (device != nullptr)
    {
        if (FT4222_UnInitialize)
            FT4222_UnInitialize(device);
        if (FT_Close)
            FT_Close(device);
        device = nullptr;
    }

    ft4222Lib.unload();
    ftd2xxLib.unload();

    // Clear function pointers
    FT_OpenEx = nullptr;
    FT_Close = nullptr;
    FT_SetTimeouts = nullptr;
    FT_SetLatencyTimer = nullptr;
    FT4222_UnInitialize = nullptr;
    FT4222_SPIMaster_Init = nullptr;
    FT4222_SPIMaster_SingleRead = nullptr;
    FT4222_SetClock = nullptr;
}

void ft4222Handler::run()
{
    if (!ftdiLoaded)
    {
        qInfo(logRig()) << "FT4222 support not available - FTDI libraries not loaded";
        while (this->running)
        {
            this->msleep(500);
        }
        return;
    }

    if (!setup())
    {
        qInfo(logRig()) << "Failed to initialize FT4222 device";
        while (this->running)
        {
            this->msleep(500);
        }
        return;
    }

    // If we got here, we are connected to a valid FT4222, so start processing
    QByteArray buf(4096, 0x0);
    FT_STATUS status;
    uint16 size;

    QElapsedTimer timer;
    timer.start();

    quint64 currentPoll = 20;
    while (this->running)
    {
        status = FT4222_SPIMaster_SingleRead(device, (uchar*)buf.data(), buf.size(), &size, false);

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
            if (poll > currentPoll + 10 || poll < currentPoll - 10)
            {
                qDebug(logRig()) << "FT4222 Scope polling changed to" << poll << "from" << currentPoll << "ms";
                currentPoll = poll;
            }
        }
    }

    if (device != nullptr)
    {
        FT4222_UnInitialize(device);
        FT_Close(device);
        device = nullptr;
    }
}

void ft4222Handler::sync()
{
    if (!ftdiLoaded || device == nullptr)
        return;

    uchar buf[16];
    uint16 size;
    int i = 0;
    int b = 0;
    while (b < 8192)
    {
        if (FT4222_OK != FT4222_SPIMaster_SingleRead(device, &buf[i], 1, &size, false) || size != 1)
        {
            qInfo(logRig()) << "FT4222 Error during sync!";
            return;
        }
        if (buf[0] != 0xff)
        {
            i = 0;
        }
        else if (i == 15)
        {
            QByteArray sync = QByteArray((char*)buf, sizeof(buf));
            if (sync == QByteArrayLiteral("\xff\x01\xee\x01\xff\x01\xee\x01\xff\x01\xee\x01\xff\x01\xee\x01"))
            {
                qInfo(logRig()) << "FT4222 re-synchronised";
                return;
            }
            i = 0;
        }
        else
        {
            ++i;
        }
        ++b;
    }

    qInfo(logRig()) << "FT4222 failed to resync";
    setup();
}

bool ft4222Handler::setup()
{
    if (!ftdiLoaded)
        return false;

    FT_STATUS ftStatus;
    FT4222_STATUS ft4222_status;

    if (device != nullptr)
    {
        FT4222_UnInitialize(device);
        FT_Close(device);
        device = nullptr;
    }

    ftStatus = FT_OpenEx((void*)"FT4222 A", FT_OPEN_BY_DESCRIPTION, &device);
    if (FT_OK != ftStatus)
    {
        qInfo(logRig()) << "Could not open FT4222 device";
        device = nullptr;
        return false;
    }

    // Set default Read and Write timeout 100 msec
    ftStatus = FT_SetTimeouts(device, 100, 100);
    if (FT_OK != ftStatus)
    {
        qInfo(logRig()) << "FT_SetTimeouts failed!";
        goto failed;
    }

    // set latency to 2, range is 2-255
    ftStatus = FT_SetLatencyTimer(device, 2);
    if (FT_OK != ftStatus)
    {
        qInfo(logRig()) << "FT_SetLatencyTimer failed!";
        goto failed;
    }

    qInfo(logRig()) << "Init FT4222 as SPI master";
    ft4222_status = FT4222_SPIMaster_Init(device, SPI_IO_SINGLE, CLK_DIV_64, CLK_IDLE_HIGH, CLK_LEADING, 0x01);
    if (FT4222_OK != ft4222_status)
    {
        qInfo(logRig()) << "Init FT4222 as SPI master device failed!";
        goto failed;
    }

    ft4222_status = FT4222_SetClock(device, SYS_CLK_24);
    if (FT4222_OK != ft4222_status)
    {
        qInfo(logRig()) << "FT4222_SetClock() Failed to set clock to SYS_CLK_24";
        goto failed;
    }

    qInfo(logRig()) << "Init FT4222 as SPI master completed successfully!";
    return true;

failed:
    FT4222_UnInitialize(device);
    FT_Close(device);
    device = nullptr;
    return false;
}
