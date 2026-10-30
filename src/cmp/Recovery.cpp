#include "cmp/Recovery.h"
#include <QSaveFile>
namespace cmp
{
Recovery::Recovery(Document *doc, QString directory, QObject *parent)
    : QObject(parent), doc_(doc), directory_(QDir(directory).absolutePath()),
      file_(QDir(directory_).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json"))
{
    timer_.setSingleShot(true);
    timer_.setInterval(2000);
    connect(&timer_, &QTimer::timeout, this, [this] { flush(); });
    connect(doc_, &Document::changed, this, [this] { schedule(); });
    connect(&doc_->undo, &QUndoStack::indexChanged, this, [this] { schedule(); });
    connect(&doc_->undo, &QUndoStack::cleanChanged, this,
            [this](bool clean)
            {
                if (clean)
                    discard();
                else
                    schedule();
            });
}
Recovery::~Recovery()
{
    if (!closed_)
        flush();
}
void Recovery::schedule()
{
    // Throttle from the first edit; continuous editing must not indefinitely defer recovery.
    if (!closed_ && !timer_.isActive())
        timer_.start(2000);
}
bool Recovery::fail(const QString &message)
{
    if (lastError_ != message)
        emit error(message);
    lastError_ = message;
    if (!closed_)
        timer_.start(10000);
    return false;
}
bool Recovery::prepare()
{
    if (lock_)
        return true;
    if (!QDir().mkpath(directory_))
    {
        return fail("Autosave unavailable: cannot create recovery directory " + directory_);
    }
    auto lock = std::make_unique<QLockFile>(file_ + ".lock");
    lock->setStaleLockTime(0); // Never steal a living session's lock based on age.
    if (!lock->tryLock(0))
    {
        return fail("Autosave unavailable: cannot lock recovery snapshot.");
    }
    lock_ = std::move(lock);
    return true;
}
bool Recovery::discard()
{
    timer_.stop();
    if (!QFile::exists(file_) || QFile::remove(file_))
    {
        lastError_.clear();
        return true;
    }
    return fail("Could not remove recovery snapshot: " + file_);
}
void Recovery::closeSession()
{
    closed_ = true;
    discard();
}
bool Recovery::flush()
{
    if (closed_)
        return true;
    timer_.stop();
    if (doc_->undo.isClean())
        return discard();
    if (!prepare())
        return false;
    const auto data =
        QJsonDocument(QJsonObject{{"format", "CMPRecovery"},
                                  {"version", 1},
                                  {"sourcePath", doc_->path},
                                  {"savedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                  {"circuit", doc_->circuit.json()}})
            .toJson(QJsonDocument::Compact);
    if (data.size() > 8 * 1024 * 1024)
    {
        return fail("Autosave failed: recovery snapshot exceeds 8 MB.");
    }
    QSaveFile file(file_);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        return fail("Autosave failed: " + file.errorString());
    }
    lastError_.clear();
    return true;
}
QVector<RecoverySnapshot> Recovery::available(QStringList &errors) const
{
    QVector<RecoverySnapshot> result;
    const QDir directory(directory_);
    for (const auto &name : directory.entryList({"*.json"}, QDir::Files, QDir::Time))
    {
        const auto path = directory.filePath(name);
        if (path == file_ || QFileInfo(path).isSymLink())
            continue;
        auto claim = std::make_shared<QLockFile>(path + ".lock");
        claim->setStaleLockTime(0);
        if (!claim->tryLock(0))
            continue;
        QFile file(path);
        QJsonParseError parseError;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        {
            errors << "Recovery snapshot unreadable or too large: " + name;
            continue;
        }
        const auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
        const auto object = json.object();
        RecoverySnapshot snapshot;
        QString error;
        snapshot.savedAt = QDateTime::fromString(object["savedAt"].toString(), Qt::ISODateWithMs);
        if (parseError.error != QJsonParseError::NoError || !json.isObject() ||
            object["format"] != "CMPRecovery" || object["version"] != 1 || !object["sourcePath"].isString() ||
            !snapshot.savedAt.isValid() || !object["circuit"].isObject() ||
            !Circuit::parse(object["circuit"].toObject(), snapshot.circuit, error))
        {
            errors << "Invalid recovery snapshot retained: " + name;
            continue;
        }
        snapshot.sourcePath = object["sourcePath"].toString();
        snapshot.file = path;
        snapshot.claim = std::move(claim);
        result << snapshot;
    }
    return result;
}
bool Recovery::remove(const RecoverySnapshot &snapshot)
{
    if (!snapshot.claim || !snapshot.claim->isLocked() ||
        QFileInfo(snapshot.file).absolutePath() != directory_ || QFileInfo(snapshot.file).isSymLink())
        return false;
    if (QFile::remove(snapshot.file))
        return true;
    emit error("Could not remove recovery snapshot: " + snapshot.file);
    return false;
}
bool Recovery::restore(const RecoverySnapshot &snapshot)
{
    if (doc_->editsLocked || !snapshot.claim || !snapshot.claim->isLocked())
        return false;
    doc_->replace(snapshot.circuit, snapshot.sourcePath);
    doc_->undo.resetClean(); // Recovered content is unsaved even when it has no undo history.
    // Commit a new owned snapshot before removing the abandoned one.
    if (flush())
        remove(snapshot);
    return true;
}
} // namespace cmp
