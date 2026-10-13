#ifndef SERVERPROCESS_H
#define SERVERPROCESS_H

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

// Runs wfweb as a child process.
//
// stdout and stderr go to the null device: the server block-buffers a piped
// stdout, so the launcher reads the per-line-flushed -l log file instead
// (see LogTail). stdin stays a pipe because a 'q' on it is the server's
// graceful quit — the same path as pressing q on its terminal status page,
// which runs the full teardown (LAN token removal, audio close, settings
// flush) that a kill() would skip.
//
// On Windows the child is created without a console window and is put in a
// Job Object that kills it if the launcher itself dies.
class ServerProcess : public QObject
{
    Q_OBJECT
public:
    enum State { Stopped, Starting, Running, Stopping };
    Q_ENUM(State)

    explicit ServerProcess(QObject *parent = nullptr);
    ~ServerProcess() override;

    void start(const QString &program, const QStringList &arguments);
    // Graceful: 'q' on stdin, then kill() if the server is still there after
    // killTimeoutMs (its own shutdown budget is 4 s).
    void stop();

    State state() const { return m_state; }
    bool isRunning() const { return m_state == Starting || m_state == Running; }

    // The command line as the user could type it; the LAN password is masked.
    QString commandLine() const;

    static const int killTimeoutMs = 10000;

signals:
    void stateChanged(ServerProcess::State state);
    void finished(int exitCode, bool crashed);
    void failedToStart(const QString &error);

private:
    void setState(State state);
    void onStarted();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);
    void closeJob();

    QProcess m_process;
    QTimer m_killTimer;
    State m_state = Stopped;
    void *m_job = nullptr; // Windows Job Object handle, null elsewhere
};

#endif // SERVERPROCESS_H
