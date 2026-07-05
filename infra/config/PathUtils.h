#pragma once

#include <QDir>
#include <QString>
#include <string>

namespace infra {

/// Find the latest numbered run subdirectory under basePath,
/// returning basePath + "/N+1" (creating a new run number).
/// If the directory doesn't exist, returns empty string.
inline std::string findLatestRunDir(const std::string& basePath) {
    QDir baseDir(QString::fromStdString(basePath));
    if (!baseDir.exists()) return {};

    int runNumber = 0;
    for (const QString& d : baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        int n = d.toInt(&ok);
        if (ok && n > runNumber) runNumber = n;
    }
    return runNumber > 0 ? basePath + "/" + std::to_string(runNumber) : basePath;
}

/// Create the next run subdirectory: basePath/N+1, creating it on disk.
/// Returns the full path to the new directory.
inline std::string createNextRunDir(const std::string& basePath) {
    QDir baseDir(QString::fromStdString(basePath));
    int runNumber = 0;
    if (baseDir.exists()) {
        for (const QString& d : baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bool ok;
            int n = d.toInt(&ok);
            if (ok && n > runNumber) runNumber = n;
        }
    } else {
        baseDir.mkpath(".");
    }
    ++runNumber;
    std::string newPath = basePath + "/" + std::to_string(runNumber);
    QDir().mkpath(QString::fromStdString(newPath));
    return newPath;
}

} // namespace infra
