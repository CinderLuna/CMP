#include "cmp/WorkerProtocol.h"
#include "cmp/WorkerSandbox.h"
#include <QtNetwork/QTcpServer>
#include <iostream>
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory(QDir::currentPath() + "/cmp-private-XXXXXX");
    if (!directory.isValid())
        return 1;
    const auto sentinel = directory.filePath("private.txt");
    QFile file(sentinel);
    if (!file.open(QIODevice::WriteOnly))
        return 2;
    file.write("private test data");
    file.close();
    QTcpServer listener;
    if (!listener.listen(QHostAddress::LocalHost, 0))
        return 7;
    WorkerSandbox sandbox;
    QProcess process;
    QString error;
    const auto executable = app.applicationDirPath() + "/cmp_worker.exe";
    if (!sandbox.prepare(process, executable, error))
    {
        std::cerr << error.toStdString();
        return 3;
    }
    process.start(executable,
                  {"--sandbox-probe", sentinel, "--port", QString::number(listener.serverPort())});
    if (!process.waitForFinished(15000))
    {
        process.kill();
        process.waitForFinished();
        return 4;
    }
    ipc::Decoder decoder;
    QVector<QJsonObject> messages;
    if (!decoder.feed(process.readAllStandardOutput(), messages, error) || messages.size() != 1)
    {
        std::cerr << error.toStdString() << " exit " << process.exitCode();
        return 5;
    }
    const auto object = messages[0];
    std::cout << "network error " << object["networkError"].toInt() << "\n";
    for (const auto &key : {"contained", "readDenied", "writeDenied", "networkDenied", "childDenied"})
    {
        std::cout << key << " " << object[key].toBool() << "\n";
        if (object[key] != true)
            return 6;
    }
    // The startup-policy workaround must fail closed if its read-only script is altered.
    const auto fakeRuntime = directory.filePath("worker.exe");
    QFile fixture(fakeRuntime);
    if (!fixture.open(QIODevice::WriteOnly))
        return 8;
    fixture.write("test fixture");
    fixture.close();
    WorkerSandbox bootstrap;
    QProcess unused;
    if (!bootstrap.prepare(unused, fakeRuntime, error))
        return 9;
    QFile script(directory.filePath("cmp-worker-state/spinit"));
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 10;
    script.write("shell unexpected command\n");
    script.close();
    WorkerSandbox rejecting;
    QProcess rejected;
    if (rejecting.prepare(rejected, fakeRuntime, error) || error.isEmpty())
        return 11;
    std::cout << "tampered startup policy rejected\n";
    return 0;
}
