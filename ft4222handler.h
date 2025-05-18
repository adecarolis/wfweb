#ifndef FT4222HANDLER_H
#define FT4222HANDLER_H

#include <QThread>
#include "packettypes.h"
#include "wfviewtypes.h"

#ifdef FTDI_SUPPORT
#include "ftd2xx.h"
#include "libft4222.h"
#endif

class ft4222Handler : public QThread
{
    Q_OBJECT
public:
    void stop() { running = false; }
    void setPoll(qint64 tm) { poll = tm; };

private:
    void run() override;

    void read();
    void sync();
    bool setup();
#ifdef FTDI_SUPPORT
    FT_HANDLE device = NULL;
    std::vector< FT_DEVICE_LIST_INFO_NODE > devList;
#endif
    bool running = true;
    qint64 poll=20;

signals:
    void dataReady(ft710_spi_data d);
};

#endif // FT4222HANDLER_H
