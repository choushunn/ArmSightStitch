#pragma once

#include <QDir>
#include <QString>
#include <QDateTime>
#include <string>

namespace infra {

/// Find the latest run subdirectory under basePath.
/// Returns the most recent timestamp-named directory, or empty.
inline std::string findLatestRunDir(const std::string& basePath) {
    QDir baseDir(QString::fromStdString(basePath));
    if (!baseDir.exists()) return {};

    QString latest;
    for (const QString& d : baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        // Timestamp dirs sort lexicographically; take the last (most recent)
        if (d.length() == 19 && d[4] == '-' && d[7] == '-' && d[10] == '_') {
            if (d > latest) latest = d;
        }
    }
    if (!latest.isEmpty())
        return basePath + "/" + latest.toStdString();
    // Fallback: check numbered dirs
    int runNumber = 0;
    for (const QString& d : baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        int n = d.toInt(&ok);
        if (ok && n > runNumber) runNumber = n;
    }
    return runNumber > 0 ? basePath + "/" + std::to_string(runNumber) : basePath;
}

/// Create a timestamp-named run subdirectory: basePath/YYYY-MM-DD_HH-MM-SS
inline std::string createNextRunDir(const std::string& basePath) {
    QDir baseDir(QString::fromStdString(basePath));
    if (!baseDir.exists()) {
        baseDir.mkpath(".");
    }
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    std::string newPath = basePath + "/" + ts.toStdString();
    QDir().mkpath(QString::fromStdString(newPath));
    return newPath;
}

} // namespace infra
