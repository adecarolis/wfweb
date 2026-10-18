// wfweb launcher: a window that composes the wfweb command line, runs the
// server as a child process, follows its log and opens the browser. The
// server binary is unchanged and stays usable from a console on its own.
//
//   wfweb-launcher            open the window
//   wfweb-launcher --start    open the window and start the server at once
//   wfweb-launcher --tray     start the server and stay in the tray (what the
//                             "start with Windows" option registers)

#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

namespace {
const char kInstanceName[] = "wfweb-launcher";
}

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QApplication app(argc, argv);
    app.setOrganizationName("wfweb");
    app.setOrganizationDomain("wfweb.org");
    app.setApplicationName("launcher");
    app.setWindowIcon(QIcon(":/wfweb.png"));
    // The tray icon keeps the launcher alive; MainWindow quits explicitly.
    app.setQuitOnLastWindowClosed(false);

    const QStringList args = app.arguments();
    const bool tray = args.contains("--tray");
    const bool start = tray || args.contains("--start");

    // One launcher at a time: a second one just brings the first forward.
    {
        QLocalSocket probe;
        probe.connectToServer(kInstanceName);
        if (probe.waitForConnected(300)) {
            probe.write("show\n");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }
    QLocalServer::removeServer(kInstanceName);
    QLocalServer instance;
    instance.listen(kInstanceName);

    MainWindow window;
    QObject::connect(&instance, &QLocalServer::newConnection, &window, [&instance, &window]() {
        while (QLocalSocket *peer = instance.nextPendingConnection())
            peer->deleteLater();
        window.showAndRaise();
    });

    if (!tray)
        window.show();
    // From inside the event loop, not before it: a refusal dialog shown
    // before exec() runs in a nested loop that a close request can leave
    // the window blocked behind.
    if (start)
        QTimer::singleShot(0, &window, &MainWindow::startServer);
    return app.exec();
}
