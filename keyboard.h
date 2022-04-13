#include <QCoreApplication>
#include <QObject>
#include <QThread>
class keyboard : public QThread
{
    Q_OBJECT
public:
    keyboard(void);
    ~keyboard(void);
    void run();
};
