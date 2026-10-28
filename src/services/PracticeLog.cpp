#include "PracticeLog.h"

#include "PracticeService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>

namespace {
constexpr int kMaxEntries = 500;
}

QString PracticeLog::logPath() {
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    return QDir(dir).absoluteFilePath(QStringLiteral("practice-log.jsonl"));
}

void PracticeLog::record(const QString &practiceId, bool passed,
                         const QString &detail) {
    if (practiceId.isEmpty()) {
        return;
    }
    QFile file(logPath());
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return;
    }
    QJsonObject row;
    row.insert(QStringLiteral("at"),
               QDateTime::currentDateTime().toString(Qt::ISODate));
    row.insert(QStringLiteral("id"), practiceId);
    row.insert(QStringLiteral("title"), PracticeService::definition(practiceId).title);
    row.insert(QStringLiteral("passed"), passed);
    row.insert(QStringLiteral("detail"), detail);
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QString::fromUtf8(QJsonDocument(row).toJson(QJsonDocument::Compact))
        << '\n';
    file.close();
}

QVector<PracticeLogEntry> PracticeLog::entries() {
    QVector<PracticeLogEntry> result;
    QFile file(logPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    const QString all = QString::fromUtf8(file.readAll());
    file.close();

    for (const QString &line : all.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QJsonObject row = QJsonDocument::fromJson(line.toUtf8()).object();
        if (row.isEmpty()) {
            continue;
        }
        PracticeLogEntry entry;
        entry.timestamp = QDateTime::fromString(
            row.value(QStringLiteral("at")).toString(), Qt::ISODate);
        entry.practiceId = row.value(QStringLiteral("id")).toString();
        entry.title = row.value(QStringLiteral("title")).toString();
        entry.passed = row.value(QStringLiteral("passed")).toBool();
        entry.detail = row.value(QStringLiteral("detail")).toString();
        result.append(entry);
    }

    // Only the newest verdicts are returned once the log grows past the cap;
    // the file itself is append-only (one short line per check, so a single
    // learner never reaches the cap).
    if (result.size() > kMaxEntries) {
        result = result.mid(result.size() - kMaxEntries);
    }
    return result;
}

QString PracticeLog::toMarkdown() {
    const auto catalog = PracticeService::catalog();
    const QVector<PracticeLogEntry> log = entries();

    // Latest verdict per practice decides "passed now"; the log keeps the
    // history a grade or a self-review would want.
    QHash<QString, bool> latest;
    for (const PracticeLogEntry &entry : log) {
        latest.insert(entry.practiceId, entry.passed);
    }

    QString md;
    QTextStream out(&md);
    out.setEncoding(QStringConverter::Utf8);
    out << "# QtGitStudio 练习记录\n\n";
    out << "导出时间：" << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        << "\n\n";

    out << "## 进度汇总\n\n";
    QStringList chapters;
    for (const PracticeDefinition &item : catalog) {
        if (!chapters.contains(item.chapter)) {
            chapters.append(item.chapter);
        }
    }
    for (const QString &chapter : chapters) {
        int passed = 0;
        int total = 0;
        for (const PracticeDefinition &item : catalog) {
            if (item.chapter != chapter) {
                continue;
            }
            ++total;
            if (latest.value(item.id, false)) {
                ++passed;
            }
        }
        out << "- " << chapter << "：" << passed << "/" << total << " 已通过\n";
    }

    out << "\n## 关卡状态\n\n";
    out << "| 章节 | 关卡 | 状态 |\n|---|---|---|\n";
    for (const PracticeDefinition &item : catalog) {
        out << "| " << item.chapter << " | " << item.title << " | "
            << (latest.value(item.id, false) ? QStringLiteral("已通过")
                                             : QStringLiteral("未通过"))
            << " |\n";
    }

    out << "\n## 判定明细\n\n";
    if (log.isEmpty()) {
        out << "（还没有任何判定记录）\n";
    } else {
        out << "| 时间 | 关卡 | 结果 | 判定原文 |\n|---|---|---|---|\n";
        for (const PracticeLogEntry &entry : log) {
            const QString when =
                entry.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            const QString verdict = entry.passed ? QStringLiteral("通过")
                                                 : QStringLiteral("未通过");
            out << "| " << when << " | " << entry.title << " | " << verdict
                << " | " << entry.detail << " |\n";
        }
    }
    return md;
}

bool PracticeLog::exportTo(const QString &filePath, QString *error) {
    QFile file(filePath);
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("无法写入 %1").arg(filePath);
        }
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << toMarkdown();
    file.close();
    return true;
}

void PracticeLog::clear() {
    QFile::remove(logPath());
}
