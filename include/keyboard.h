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
signals:
    // Every key read from the terminal, for the status page ("l" etc.).
    void keyPressed(char key);
};
