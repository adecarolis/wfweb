#include "portowner.h"

#include <QFileInfo>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <winternl.h>
#include <vector>
#endif

void PortOwner::classify(const QString &launcherLogPath)
{
    const QString base = QFileInfo(image).completeBaseName();
    isWfweb = base.compare(QStringLiteral("wfweb"), Qt::CaseInsensitive) == 0;
    // A bare --lan, not --lan-user / --lan-pass / --lan-control.
    isLan = commandLine.contains(QRegularExpression("(^|\\s)--lan\\s"));
    // Compare with one separator style regardless of the host: the command
    // line is a Windows one even when this code runs in the Linux CI test,
    // where QDir::fromNativeSeparators would leave backslashes alone.
    QString command = commandLine;
    QString logPath = launcherLogPath;
    command.replace('\\', '/');
    logPath.replace('\\', '/');
    fromLauncher = !logPath.isEmpty() && command.contains(logPath, Qt::CaseInsensitive);
}

QString PortOwner::masked(const QString &commandLine)
{
    QString s = commandLine;
    s.replace(QRegularExpression("(--lan-pass\\s+)(\"[^\"]*\"|\\S+)"), "\\1****");
    return s;
}

#ifdef Q_OS_WIN
namespace {

qint64 listeningPid(quint16 port, ULONG family)
{
    DWORD size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (size == 0)
        return 0;
    std::vector<unsigned char> buffer(size);
    if (GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
        return 0;

    if (family == AF_INET) {
        const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCPROW_OWNER_PID &row = table->table[i];
            if (ntohs(static_cast<u_short>(row.dwLocalPort)) == port)
                return row.dwOwningPid;
        }
    } else {
        const auto *table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const MIB_TCP6ROW_OWNER_PID &row = table->table[i];
            if (ntohs(static_cast<u_short>(row.dwLocalPort)) == port)
                return row.dwOwningPid;
        }
    }
    return 0;
}

QString imageNameOf(HANDLE process)
{
    wchar_t path[MAX_PATH * 2];
    DWORD length = sizeof(path) / sizeof(path[0]);
    if (!QueryFullProcessImageNameW(process, 0, path, &length))
        return QString();
    return QFileInfo(QString::fromWCharArray(path, int(length))).fileName();
}

typedef NTSTATUS(NTAPI *NtQueryInformationProcessFn)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

// The command line lives in the other process's PEB. Same bitness and same
// user are needed, which holds for a wfweb started by this user.
QString commandLineOf(HANDLE process)
{
    static const auto query = reinterpret_cast<NtQueryInformationProcessFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (!query)
        return QString();

    PROCESS_BASIC_INFORMATION info{};
    ULONG returned = 0;
    if (query(process, ProcessBasicInformation, &info, sizeof(info), &returned) != 0 || !info.PebBaseAddress)
        return QString();

    PEB peb{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, info.PebBaseAddress, &peb, sizeof(peb), &read) || !peb.ProcessParameters)
        return QString();

    RTL_USER_PROCESS_PARAMETERS params{};
    if (!ReadProcessMemory(process, peb.ProcessParameters, &params, sizeof(params), &read))
        return QString();

    const USHORT bytes = params.CommandLine.Length;
    if (bytes == 0 || !params.CommandLine.Buffer)
        return QString();
    std::vector<wchar_t> text(bytes / sizeof(wchar_t));
    if (!ReadProcessMemory(process, params.CommandLine.Buffer, text.data(), bytes, &read))
        return QString();
    return QString::fromWCharArray(text.data(), int(read / sizeof(wchar_t)));
}

} // namespace
#endif

PortOwner PortOwner::lookup(quint16 port, const QString &launcherLogPath)
{
    PortOwner owner;
#ifdef Q_OS_WIN
    qint64 pid = listeningPid(port, AF_INET);
    if (pid == 0)
        pid = listeningPid(port, AF_INET6);
    if (pid == 0)
        return owner;
    owner.found = true;
    owner.pid = pid;

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if (!process)
        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process) {
        owner.image = imageNameOf(process);
        owner.commandLine = commandLineOf(process);
        CloseHandle(process);
    }
#else
    Q_UNUSED(port);
#endif
    owner.classify(launcherLogPath);
    return owner;
}

bool PortOwner::terminate(qint64 pid, QString *error)
{
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        if (error)
            *error = QStringLiteral("could not open process %1 (Windows error %2)").arg(pid).arg(GetLastError());
        return false;
    }
    const bool ok = TerminateProcess(process, 1) != 0;
    if (ok)
        WaitForSingleObject(process, 5000);
    else if (error)
        *error = QStringLiteral("TerminateProcess failed (Windows error %1)").arg(GetLastError());
    CloseHandle(process);
    return ok;
#else
    Q_UNUSED(pid);
    if (error)
        *error = QStringLiteral("not supported on this platform");
    return false;
#endif
}
