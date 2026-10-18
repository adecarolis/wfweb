#include "serverprocess.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

ServerProcess::ServerProcess(QObject *parent)
    : QObject(parent)
{
    m_process.setStandardOutputFile(QProcess::nullDevice());
    m_process.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    // No console window, whether or not the launcher itself has one. The
    // server still gets its stdin pipe: STARTF_USESTDHANDLES is unaffected.
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    m_killTimer.setSingleShot(true);
    m_killTimer.setInterval(killTimeoutMs);
    connect(&m_killTimer, &QTimer::timeout, this, [this]() {
        if (m_process.state() != QProcess::NotRunning)
            m_process.kill();
    });

    connect(&m_process, &QProcess::started, this, &ServerProcess::onStarted);
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ServerProcess::onFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &ServerProcess::onError);
}

ServerProcess::~ServerProcess()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.write("q\n");
        m_process.waitForBytesWritten(1000);
        if (!m_process.waitForFinished(5000)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
    closeJob();
}

void ServerProcess::start(const QString &program, const QStringList &arguments)
{
    if (m_process.state() != QProcess::NotRunning)
        return;
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    // Not strictly needed (the server finds rigs/ next to its own exe), but
    // keeps relative paths in "extra arguments" predictable.
    m_process.setWorkingDirectory(QFileInfo(program).absolutePath());
    setState(Starting);
    m_process.start(QIODevice::ReadWrite);
}

void ServerProcess::stop()
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    setState(Stopping);
    m_process.write("q\n");
    m_killTimer.start();
}

QString ServerProcess::commandLine() const
{
    QStringList parts{QDir::toNativeSeparators(m_process.program())};
    bool password = false;
    for (const QString &arg : m_process.arguments()) {
        QString shown = password ? QStringLiteral("****") : arg;
        if (shown.contains(' '))
            shown = '"' + shown + '"';
        parts << shown;
        password = (arg == "--lan-pass");
    }
    return parts.join(' ');
}

void ServerProcess::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void ServerProcess::onStarted()
{
#ifdef Q_OS_WIN
    // Tie the server's lifetime to the launcher's: if the launcher goes away
    // without stopping it, Windows closes the job and ends the server.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                     static_cast<DWORD>(m_process.processId()));
        if (process) {
            AssignProcessToJobObject(job, process);
            CloseHandle(process);
        }
        m_job = job;
    }
#endif
    setState(Running);
}

void ServerProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_killTimer.stop();
    closeJob();
    setState(Stopped);
    emit finished(exitCode, status == QProcess::CrashExit);
}

void ServerProcess::onError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    m_killTimer.stop();
    setState(Stopped);
    emit failedToStart(m_process.errorString());
}

void ServerProcess::closeJob()
{
#ifdef Q_OS_WIN
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
#endif
}
