#ifndef FT4222HANDLER_H
#define FT4222HANDLER_H

#include <QThread>
#include "packettypes.h"
#include "wfviewtypes.h"

#include "ftd2xx.h"
#include "LibFT4222.h"

class ft4222Handler : public QThread
{
    Q_OBJECT
public:
    void stop() { running = false; }

private:
    void run() override;

    void read();
    void findDevices();
    void sync();
    bool setup(uchar num);
    FT_HANDLE device = NULL;
    std::vector< FT_DEVICE_LIST_INFO_NODE > devList;
    bool running = true;

signals:
    void dataReady(scopeData data, freqt a, freqt b);
};

#endif // FT4222HANDLER_H
