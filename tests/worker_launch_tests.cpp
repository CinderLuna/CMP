#include "cmp/WorkerProtocol.h"
#include "cmp/WorkerSandbox.h"
#include <iostream>
#include <memory>
#include <vector>
using namespace cmp;

static int launches(const QString &worker)
{
    for (int round = 0; round < 20; ++round)
    {
        WorkerSandbox sandbox;
        QProcess process;
        QString error;
        if (!sandbox.prepare(process, worker, error))
        {
            std::cerr << "round " << round << ": " << error.toStdString() << '\n';
            return 1;
        }
        process.start(worker, {"--sandbox-check"});
        if (!process.waitForStarted(10000) || !process.waitForFinished(10000))
        {
            std::cerr << "round " << round << ": " << process.errorString().toStdString() << '\n';
            process.kill();
            process.waitForFinished(2000);
            return 2;
        }
        ipc::Decoder decoder;
        QVector<QJsonObject> messages;
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
            !decoder.feed(process.readAllStandardOutput(), messages, error) || messages.size() != 1 ||
            messages[0].value("contained") != true)
        {
            std::cerr << "round " << round << ": invalid containment response, exit " << process.exitCode()
                      << ' ' << error.toStdString() << '\n';
            return 3;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto worker = app.applicationDirPath() + "/cmp_worker.exe";
    if (app.arguments().contains("--child"))
        return launches(worker);

    // Four independent editor processes repeatedly initialize the same profile and runtime.
    // Every launched worker must attest the full AppContainer and job restrictions.
    std::vector<std::unique_ptr<QProcess>> children;
    for (int index = 0; index < 4; ++index)
    {
        auto process = std::make_unique<QProcess>();
        process->start(app.applicationFilePath(), {"--child"});
        children.push_back(std::move(process));
    }
    bool ok = true;
    for (auto &process : children)
    {
        if (!process->waitForFinished(45000))
        {
            process->kill();
            process->waitForFinished(2000);
            ok = false;
        }
        ok &= process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
        std::cerr << process->readAllStandardError().toStdString();
    }
    if (!ok)
        return 1;

    // DWORD-returning ACL APIs must report their actual failure, not a stale GetLastError().
    QTemporaryDir directory;
    if (!directory.isValid())
        return 2;
    WorkerSandbox sandbox;
    QProcess process;
    QString error;
    if (sandbox.prepare(process, directory.filePath("missing.exe"), error) ||
        !error.contains("read runtime ACL") || !error.contains("0x00000002"))
    {
        std::cerr << "Missing runtime diagnostic: " << error.toStdString() << '\n';
        return 3;
    }
    std::cout << "80 concurrent/repeated contained worker launches and exact ACL error passed\n";
    return 0;
}
