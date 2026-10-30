#include "cmp/Recovery.h"
#include <QProcess>
#include <cstdlib>
#include <iostream>
using namespace cmp;
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "--crash-writer")
    {
        Document doc;
        Recovery recovery(&doc, app.arguments().value(2));
        doc.path = app.arguments().value(3);
        doc.edit("Unsaved resistor", [](Circuit &c) { c.part("RA")->value = 12345; });
        if (!recovery.flush())
            return 2;
        std::_Exit(0); // Deliberately bypass cleanup, leaving a real stale lock and snapshot.
    }
    int failed = 0;
    auto check = [&](const char *name, bool pass)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << "\n";
        failed += !pass;
    };
    QDir().mkpath("work");
    QTemporaryDir temp(QDir::current().filePath("work/recovery-tests-XXXXXX"));
    check("temporary recovery directory created", temp.isValid());
    const auto directory = temp.filePath("recovery");
    Document doc;
    Recovery recovery(&doc, directory);
    QObject::connect(&recovery, &Recovery::error, &app,
                     [](const QString &s) { std::cerr << s.toStdString() << "\n"; });
    QStringList errors;
    check("clean document writes no snapshot", recovery.flush() && !QFile::exists(recovery.snapshotPath()));
    doc.edit("Unsaved", [](Circuit &c) { c.part("RA")->value = 20000; });
    check("atomic dirty snapshot", recovery.flush() && QFile::exists(recovery.snapshotPath()));
    Document other;
    Recovery observer(&other, directory);
    check("active session excluded", observer.available(errors).isEmpty());
    QString saveError;
    const auto original = temp.filePath("original.cmp");
    check("explicit save removes snapshot",
          doc.save(original, saveError) && !QFile::exists(recovery.snapshotPath()));
    doc.edit("Another edit", [](Circuit &c) { c.part("RA")->value = 30000; });
    recovery.flush();
    Document originalDoc;
    check("autosave never overwrites original",
          originalDoc.load(original, saveError) && originalDoc.circuit.part("RA")->value == 20000);
    doc.undo.undo();
    check("undo to saved clears snapshot", !QFile::exists(recovery.snapshotPath()));
    QProcess child;
    child.start(app.applicationFilePath(), {"--crash-writer", directory, original});
    check("interrupted writer exits", child.waitForFinished(10000) && child.exitCode() == 0);
    auto snapshots = observer.available(errors);
    check("stale process lock recovered", snapshots.size() == 1);
    if (snapshots.size() == 1)
    {
        check("recovery snapshot contains unsaved edit",
              snapshots[0].circuit.part("RA")->value == 12345 && snapshots[0].sourcePath == original);
        check("claimed recovery excluded from other windows", recovery.available(errors).isEmpty());
        const auto previous = snapshots[0].file;
        check("restore successful", observer.restore(snapshots[0]));
        check("restored document remains dirty",
              !other.undo.isClean() && other.path == original && other.circuit.part("RA")->value == 12345);
        check("new owned snapshot precedes old removal",
              QFile::exists(observer.snapshotPath()) && !QFile::exists(previous));
        check("original unchanged after recovery",
              originalDoc.load(original, saveError) && originalDoc.circuit.part("RA")->value == 20000);
    }
    const auto corrupt = temp.filePath("recovery/corrupt.json");
    {
        QFile file(corrupt);
        file.open(QIODevice::WriteOnly);
        file.write("{broken");
    }
    errors.clear();
    observer.available(errors);
    check("corrupt snapshot retained with diagnostic", !errors.isEmpty() && QFile::exists(corrupt));
    QString closedPath;
    {
        Document closing;
        Recovery session(&closing, directory);
        closing.edit("Unsaved", [](Circuit &c) { c.part("RA")->value = 7; });
        session.flush();
        closedPath = session.snapshotPath();
        session.closeSession();
    }
    check("intentional discard not recreated at destruction", !QFile::exists(closedPath));
    const auto blocked = temp.filePath("not-a-directory");
    {
        QFile file(blocked);
        file.open(QIODevice::WriteOnly);
        file.write("x");
    }
    Document blockedDoc;
    Recovery blockedRecovery(&blockedDoc, blocked);
    bool notified = false;
    QObject::connect(&blockedRecovery, &Recovery::error, &app, [&](QString) { notified = true; });
    blockedDoc.edit("Unsaved", [](Circuit &c) { c.part("RA")->value = 8; });
    check("unwritable recovery reports failure without losing document",
          !blockedRecovery.flush() && notified && blockedDoc.circuit.part("RA")->value == 8);
    child.start(app.applicationFilePath(), {"--crash-writer", directory, original});
    child.waitForFinished(10000);
    auto preserved = observer.available(errors);
    if (preserved.size() == 1)
    {
        check("failed ownership transfer preserves original snapshot",
              blockedRecovery.restore(preserved[0]) && QFile::exists(preserved[0].file));
        check("failed autosave still restores document in memory",
              blockedDoc.circuit.part("RA")->value == 12345 && !blockedDoc.undo.isClean());
    }
    else
        check("second crash snapshot found", false);
    blockedRecovery.closeSession();
    Document timed;
    Recovery timerRecovery(&timed, temp.filePath("timed"));
    timed.edit("Timed", [](Circuit &c) { c.part("RA")->value = 9; });
    QEventLoop loop;
    QTimer::singleShot(2300, &loop, &QEventLoop::quit);
    loop.exec();
    check("automatic snapshot within recovery interval", QFile::exists(timerRecovery.snapshotPath()));
    timerRecovery.closeSession();
    observer.closeSession();
    recovery.closeSession();
    return failed ? 1 : 0;
}
