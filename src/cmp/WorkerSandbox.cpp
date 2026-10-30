#include "cmp/WorkerSandbox.h"
#include <cstring>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#include <windows.h>
#endif
namespace cmp
{
#ifdef Q_OS_WIN
static QString launchError(const QString &stage, quint32 code)
{
    return QString("Cannot establish restricted simulation worker: %1 (Windows status 0x%2).")
        .arg(stage, QString::number(code, 16).rightJustified(8, '0'));
}
class ProvisionLock
{
  public:
    HANDLE handle = nullptr;
    bool owned = false;
    ~ProvisionLock()
    {
        if (owned)
            ReleaseMutex(handle);
        if (handle)
            CloseHandle(handle);
    }
    bool acquire(QString &error)
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            error = launchError("query provisioning identity", GetLastError());
            return false;
        }
        QVector<quint64> storage((sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE + 7) / 8);
        DWORD bytes = 0;
        const bool queried = GetTokenInformation(token, TokenUser, storage.data(),
                                                 DWORD(storage.size() * sizeof(quint64)), &bytes);
        const auto status = queried ? ERROR_SUCCESS : GetLastError();
        CloseHandle(token);
        LPWSTR identity = nullptr;
        if (!queried ||
            !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid, &identity))
        {
            error = launchError("read provisioning identity", queried ? GetLastError() : status);
            return false;
        }
        // Profile storage is per user, including across desktop/RDP sessions. Different users
        // must not contend for a mutex they cannot open under its default token DACL.
        const auto name =
            (QString("Global\\Cinderluna.CMP.WorkerProvision.v1.") + QString::fromWCharArray(identity))
                .toStdWString();
        LocalFree(identity);
        handle = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!handle)
        {
            error = launchError("create provisioning lock", GetLastError());
            return false;
        }
        const auto result = WaitForSingleObject(handle, 5000);
        owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        if (!owned)
            error = launchError("wait for worker provisioning",
                                result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
        return owned;
    }
};
#endif
struct WorkerSandbox::Impl
{
#ifdef Q_OS_WIN
    PSID sid = nullptr;
    HANDLE job = nullptr;
    SECURITY_CAPABILITIES capabilities{};
    STARTUPINFOEXW startup{};
    HANDLE inherited[3]{};
    QByteArray storage;
    ~Impl()
    {
        if (startup.lpAttributeList)
            DeleteProcThreadAttributeList(startup.lpAttributeList);
        if (job)
            CloseHandle(job);
        if (sid)
            FreeSid(sid);
    }
    bool grantRead(const QString &path, QString &error)
    {
        auto wide = QDir::toNativeSeparators(path).toStdWString();
        PACL original = nullptr, merged = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        DWORD result = GetNamedSecurityInfoW(wide.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                             nullptr, &original, nullptr, &descriptor);
        if (result != ERROR_SUCCESS)
        {
            error = launchError("read runtime ACL: " + path, result);
            return false;
        }
        // Installer-provided or previously granted runtime access needs no ACL mutation.
        BYTE allPackages[SECURITY_MAX_SID_SIZE];
        DWORD allPackagesSize = sizeof(allPackages);
        const bool havePackages =
            CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, allPackages, &allPackagesSize);
        for (DWORD index = 0; original && index < original->AceCount; ++index)
        {
            void *raw = nullptr;
            if (!GetAce(original, index, &raw))
                continue;
            const auto *header = static_cast<ACE_HEADER *>(raw);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || (header->AceFlags & INHERIT_ONLY_ACE))
                continue;
            const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
            auto trustee = reinterpret_cast<PSID>(const_cast<DWORD *>(&ace->SidStart));
            if (!EqualSid(trustee, sid) && !(havePackages && EqualSid(trustee, allPackages)))
                continue;
            constexpr DWORD rights = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
            if ((ace->Mask & rights) == rights ||
                (ace->Mask & (GENERIC_READ | GENERIC_EXECUTE)) == (GENERIC_READ | GENERIC_EXECUTE))
            {
                LocalFree(descriptor);
                return true;
            }
        }
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
        access.grfAccessMode = GRANT_ACCESS;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
        result = SetEntriesInAclW(1, &access, original, &merged);
        if (result == ERROR_SUCCESS)
            result = SetNamedSecurityInfoW(wide.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                           nullptr, merged, nullptr);
        if (merged)
            LocalFree(merged);
        LocalFree(descriptor);
        if (result != ERROR_SUCCESS)
            error = launchError("grant runtime read/execute: " + path, result);
        return result == ERROR_SUCCESS;
    }
#endif
};
WorkerSandbox::WorkerSandbox() : p(std::make_unique<Impl>()) {}
WorkerSandbox::~WorkerSandbox() = default;
bool WorkerSandbox::prepare(QProcess &process, const QString &executable, QString &error)
{
#ifdef Q_OS_WIN
    // Windows profile initialization is not safe to race across CMP processes (E_UNEXPECTED).
    // The same lock also protects runtime ACL read/modify/write and initial spinit creation.
    // Default mutex ACLs remain user-scoped; timeout/denial never bypasses containment.
    ProvisionLock provisioning;
    if (!provisioning.acquire(error))
        return false;
    auto fail = [&](const QString &stage, quint32 code)
    {
        error = launchError(stage, code);
        return false;
    };
    HRESULT result = CreateAppContainerProfile(L"Cinderluna.CMP.Simulation.v1", L"CMP simulation",
                                               L"Isolated CMP simulation runtime", nullptr, 0, &p->sid);
    if (FAILED(result))
    {
        if (result != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
            return fail("create AppContainer profile", quint32(result));
        result = DeriveAppContainerSidFromAppContainerName(L"Cinderluna.CMP.Simulation.v1", &p->sid);
        if (FAILED(result))
            return fail("derive AppContainer SID", quint32(result));
    }
    // Only runtime files receive read/execute permission, never the project/source tree.
    QDir directory(QFileInfo(executable).absolutePath());
    if (!p->grantRead(directory.absolutePath(), error) || !p->grantRead(executable, error))
        return false;
    for (const auto &file : directory.entryList({"*.dll", "qt.conf"}, QDir::Files))
        if (!p->grantRead(directory.filePath(file), error))
            return false;
    const auto statePath = directory.filePath("cmp-worker-state");
    if (!QDir().mkpath(statePath))
    {
        error = "Cannot create isolated worker startup directory";
        return false;
    }
    const auto nativeState = QDir::toNativeSeparators(statePath).toStdWString();
    const DWORD stateAttributes = GetFileAttributesW(nativeState.c_str());
    if (stateAttributes == INVALID_FILE_ATTRIBUTES || (stateAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        error = "Worker startup directory must not be a link";
        return false;
    }
    QDir stateDirectory(statePath);
    for (const auto &name :
         stateDirectory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        if (name != "spinit")
        {
            error = "Unexpected file in isolated worker startup directory";
            return false;
        }
    const QByteArray script = "* CMP isolated runtime; no external startup commands.\n";
    const auto scriptPath = stateDirectory.filePath("spinit");
    const auto nativeScript = QDir::toNativeSeparators(scriptPath).toStdWString();
    if (QFileInfo::exists(scriptPath))
    {
        if (GetFileAttributesW(nativeScript.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            error = "Worker startup script must not be a link";
            return false;
        }
        QFile file(scriptPath);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 128 ||
            file.readAll().replace("\r\n", "\n") != script)
        {
            error = "Worker startup script differs from the bundled policy";
            return false;
        }
    }
    else
    {
        QSaveFile file(scriptPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(script) != script.size() || !file.commit())
        {
            error = "Cannot create worker startup policy";
            return false;
        }
    }
    if (!p->grantRead(statePath, error) || !p->grantRead(scriptPath, error))
        return false;
    process.setWorkingDirectory(statePath);
    p->job = CreateJobObjectW(nullptr, nullptr);
    if (!p->job)
        return fail("create worker job", GetLastError());
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                              JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                                              JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_PROCESS_TIME;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = 120LL * 10000000;
    limits.ProcessMemoryLimit = 1024ULL * 1024 * 1024;
    if (!SetInformationJobObject(p->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return fail("set worker job limits", GetLastError());
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 3, 0, &size);
    p->storage.resize(qsizetype(size));
    p->startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(p->storage.data());
    if (!InitializeProcThreadAttributeList(p->startup.lpAttributeList, 3, 0, &size))
    {
        p->startup.lpAttributeList = nullptr;
        return fail("initialize process attributes", GetLastError());
    }
    p->capabilities.AppContainerSid = p->sid; // No network or other capabilities.
    if (!UpdateProcThreadAttribute(p->startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                   &p->capabilities, sizeof(p->capabilities), nullptr, nullptr) ||
        !UpdateProcThreadAttribute(p->startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &p->job,
                                   sizeof(p->job), nullptr, nullptr))
        return fail("set containment attributes", GetLastError());
    process.setCreateProcessArgumentsModifier(
        [this](QProcess::CreateProcessArguments *args)
        {
            p->startup.StartupInfo = *reinterpret_cast<STARTUPINFOW *>(args->startupInfo);
            p->startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
            // Qt supplies only the stdio pipes; explicitly limit handle inheritance.
            p->inherited[0] = p->startup.StartupInfo.hStdInput;
            p->inherited[1] = p->startup.StartupInfo.hStdOutput;
            p->inherited[2] = p->startup.StartupInfo.hStdError;
            if (!UpdateProcThreadAttribute(p->startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                           p->inherited, sizeof(p->inherited), nullptr, nullptr))
            {
                args->applicationName = L"";
                return;
            }
            args->startupInfo = reinterpret_cast<Q_STARTUPINFO *>(&p->startup);
            args->flags |= EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
        });
    return true;
#else
    Q_UNUSED(process);
    Q_UNUSED(executable);
    error = "A restricted worker launcher is required on this platform.";
    return false;
#endif
}
bool WorkerSandbox::isContained()
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    auto query = [&](TOKEN_INFORMATION_CLASS kind, QVector<quint64> &storage)
    {
        DWORD bytes = 0;
        GetTokenInformation(token, kind, nullptr, 0, &bytes);
        if (!bytes || bytes > 8192)
            return false;
        storage.fill(0, (bytes + 7) / 8);
        return bool(GetTokenInformation(token, kind, storage.data(), bytes, &bytes));
    };
    DWORD contained = 0, bytes = 0;
    bool ok =
        GetTokenInformation(token, TokenIsAppContainer, &contained, sizeof(contained), &bytes) && contained;
    QVector<quint64> identity, capabilities;
    PSID expected = nullptr;
    if (ok)
        ok = query(TokenAppContainerSid, identity) && query(TokenCapabilities, capabilities) &&
             SUCCEEDED(DeriveAppContainerSidFromAppContainerName(L"Cinderluna.CMP.Simulation.v1", &expected));
    if (ok)
    {
        const auto *information =
            reinterpret_cast<const TOKEN_APPCONTAINER_INFORMATION *>(identity.constData());
        DWORD capabilityCount = 0;
        std::memcpy(&capabilityCount, capabilities.constData(), sizeof(capabilityCount));
        ok = information->TokenAppContainer && IsValidSid(information->TokenAppContainer) &&
             EqualSid(information->TokenAppContainer, expected) && capabilityCount == 0;
    }
    if (expected)
        FreeSid(expected);
    CloseHandle(token);
    if (!ok)
        return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits),
                                   nullptr))
        return false;
    constexpr DWORD required = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                               JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_PROCESS_TIME;
    const auto &basic = limits.BasicLimitInformation;
    return (basic.LimitFlags & required) == required &&
           !(basic.LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) &&
           basic.ActiveProcessLimit == 1 && limits.ProcessMemoryLimit > 0 &&
           limits.ProcessMemoryLimit <= 1024ULL * 1024 * 1024 && basic.PerProcessUserTimeLimit.QuadPart > 0 &&
           basic.PerProcessUserTimeLimit.QuadPart <= 120LL * 10000000;
#else
    return false;
#endif
}
} // namespace cmp
