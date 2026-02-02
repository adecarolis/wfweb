#ifndef FT4222HANDLER_H
#define FT4222HANDLER_H

#include <QThread>
#include <QElapsedTimer>
#include <QLibrary>
#include "packettypes.h"
#include "wfviewtypes.h"

// Forward declare types instead of including headers
typedef void* FT_HANDLE;
typedef unsigned long FT_STATUS;
typedef unsigned short uint16;
typedef unsigned char uchar;

// FT_STATUS values
#define FT_OK 0
#define FT_INVALID_HANDLE 1
#define FT_DEVICE_NOT_FOUND 2
#define FT_DEVICE_NOT_OPENED 3
#define FT_IO_ERROR 4
#define FT_INSUFFICIENT_RESOURCES 5
#define FT_INVALID_PARAMETER 6

// FT4222_STATUS values
#define FT4222_OK 0
typedef unsigned long FT4222_STATUS;

// Enums needed for FT4222
enum FT4222_SPIMode {
    SPI_IO_SINGLE = 1,
    SPI_IO_DUAL = 2,
    SPI_IO_QUAD = 4
};

enum FT4222_SPIClock {
    CLK_NONE = 0,
    CLK_DIV_2,
    CLK_DIV_4,
    CLK_DIV_8,
    CLK_DIV_16,
    CLK_DIV_32,
    CLK_DIV_64,
    CLK_DIV_128,
    CLK_DIV_256,
    CLK_DIV_512
};

enum FT4222_SPICPOL {
    CLK_IDLE_LOW = 0,
    CLK_IDLE_HIGH = 1
};

enum FT4222_SPICPHA {
    CLK_LEADING = 0,
    CLK_TRAILING = 1
};

enum FT4222_ClockRate {
    SYS_CLK_60 = 0,
    SYS_CLK_24,
    SYS_CLK_48,
    SYS_CLK_80
};

enum FT_OPEN_BY {
    FT_OPEN_BY_SERIAL_NUMBER = 1,
    FT_OPEN_BY_DESCRIPTION = 2,
    FT_OPEN_BY_LOCATION = 4
};

// Define calling convention for Windows (stdcall for LibFT4222 v1.4.6+)
#ifdef Q_OS_WIN
#define FTAPI __stdcall
#else
#define FTAPI
#endif

class ft4222Handler : public QThread
{
    Q_OBJECT
public:
    ft4222Handler();
    ~ft4222Handler();

    void stop() { running = false; }
    bool isFTDIAvailable() const { return ftdiLoaded; }

public slots:
    void setPoll(qint64 tm) { poll = tm; };

private:
    void run() override;
    void read();
    void sync();
    bool setup();
    bool loadFTDILibraries();
    void unloadFTDILibraries();

    // Function pointer types with proper calling convention
    typedef FT_STATUS (FTAPI *FT_OpenExFunc)(void*, unsigned long, FT_HANDLE*);
    typedef FT_STATUS (FTAPI *FT_CloseFunc)(FT_HANDLE);
    typedef FT_STATUS (FTAPI *FT_SetTimeoutsFunc)(FT_HANDLE, unsigned long, unsigned long);
    typedef FT_STATUS (FTAPI *FT_SetLatencyTimerFunc)(FT_HANDLE, unsigned char);
    typedef FT4222_STATUS (FTAPI *FT4222_UnInitializeFunc)(FT_HANDLE);
    typedef FT4222_STATUS (FTAPI *FT4222_SPIMaster_InitFunc)(FT_HANDLE, FT4222_SPIMode, FT4222_SPIClock, FT4222_SPICPOL, FT4222_SPICPHA, unsigned char);
    typedef FT4222_STATUS (FTAPI *FT4222_SPIMaster_SingleReadFunc)(FT_HANDLE, unsigned char*, unsigned short, unsigned short*, bool);
    typedef FT4222_STATUS (FTAPI *FT4222_SetClockFunc)(FT_HANDLE, FT4222_ClockRate);

    // Function pointers
    FT_OpenExFunc FT_OpenEx = nullptr;
    FT_CloseFunc FT_Close = nullptr;
    FT_SetTimeoutsFunc FT_SetTimeouts = nullptr;
    FT_SetLatencyTimerFunc FT_SetLatencyTimer = nullptr;
    FT4222_UnInitializeFunc FT4222_UnInitialize = nullptr;
    FT4222_SPIMaster_InitFunc FT4222_SPIMaster_Init = nullptr;
    FT4222_SPIMaster_SingleReadFunc FT4222_SPIMaster_SingleRead = nullptr;
    FT4222_SetClockFunc FT4222_SetClock = nullptr;

    // Library handles
    QLibrary ftd2xxLib;
    QLibrary ft4222Lib;

    FT_HANDLE device = nullptr;
    bool running = true;
    bool ftdiLoaded = false;
    quint64 poll = 20;

signals:
    void dataReady(QByteArray d);
};

#endif // FT4222HANDLER_H
