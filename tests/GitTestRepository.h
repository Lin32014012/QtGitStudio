#pragma once
#include <QTemporaryDir>
#include <QString>
#include <memory>

class GitTestRepository {
public:
    GitTestRepository();
    ~GitTestRepository();
    QString path() const { return m_dir->path(); }
    bool isValid() const { return m_dir && m_dir->isValid(); }
private:
    bool runGit(const QStringList &args);
    std::unique_ptr<QTemporaryDir> m_dir;
};
