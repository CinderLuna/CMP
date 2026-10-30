#pragma once
#include "cmp/Circuit.h"
#include <memory>
namespace cmp
{
struct RecoverySnapshot
{
    QString file, sourcePath;
    QDateTime savedAt;
    Circuit circuit;
    std::shared_ptr<QLockFile> claim;
};
class Recovery : public QObject
{
    Q_OBJECT
  public:
    explicit Recovery(Document *, QString directory, QObject *parent = nullptr);
    ~Recovery() override;
    bool flush();
    bool discard();
    void closeSession();
    QVector<RecoverySnapshot> available(QStringList &errors) const;
    bool restore(const RecoverySnapshot &);
    bool remove(const RecoverySnapshot &);
    QString snapshotPath() const
    {
        return file_;
    }
  signals:
    void error(QString);

  private:
    Document *doc_;
    QString directory_, file_, lastError_;
    QTimer timer_;
    std::unique_ptr<QLockFile> lock_;
    bool closed_ = false;
    bool prepare();
    bool fail(const QString &);
    void schedule();
};
} // namespace cmp
