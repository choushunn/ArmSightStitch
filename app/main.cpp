#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStandardPaths>

#include "ui/MainWindow.h"
#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"
#include "infra/log/LogManager.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    a.setApplicationName("ScannerApp");
    a.setApplicationVersion("2.0.0");
    a.setOrganizationName("");

    // Load configuration from JSON file if available
    auto& cfg = ConfigManager::instance();
    QString configPath = QApplication::applicationDirPath() + "/config.json";
    if (QFileInfo::exists(configPath)) {
        cfg.loadFromFile(configPath.toStdString());
    }

    // Initialize spdlog logging
    std::string logPath = cfg.logPath();
    if (!logPath.empty()) {
        QDir().mkpath(QFileInfo(QString::fromStdString(logPath)).absolutePath());
    }
    infra::LogManager::init(logPath);
    SPDLOG_INFO("Application started");

    if (QFileInfo::exists(configPath)) {
        SPDLOG_INFO("Configuration loaded from {}", configPath.toStdString());
    } else {
        SPDLOG_INFO("No config file found, using defaults");
    }

    // ── Ensure Z-map template exists in Documents ──
    auto ensureTemplate = [&](const std::string& cfgPath, const std::string& templateFileName,
                               const std::string& label) {
        QString targetPath = QString::fromStdString(cfgPath);
        if (!targetPath.isEmpty() && !QFileInfo::exists(targetPath)) {
            QString templatePath = QApplication::applicationDirPath() + "/" + QString::fromStdString(templateFileName);
            if (QFileInfo::exists(templatePath)) {
                QDir().mkpath(QFileInfo(targetPath).absolutePath());
                if (QFile::copy(templatePath, targetPath)) {
                    SPDLOG_INFO("{} template copied to {}", label, targetPath.toStdString());
                } else {
                    SPDLOG_WARN("Failed to copy {} template to {}", label, targetPath.toStdString());
                }
            }
        }
    };
    ensureTemplate(cfg.zMapFile(), "z_map_template.json", "Z-map");
    ensureTemplate(cfg.scaleMapFile(), "scale_map_template.json", "Scale-map");

    // Create application controller and main window (MVP)
    AppController ctrl;
    MainWindow w(ctrl);
    w.resize(1728, 972);   // 1920*0.9 × 1080*0.9
    w.showMaximized();

    SPDLOG_INFO("Application entering event loop");
    int result = a.exec();
    SPDLOG_INFO("Application exited with code {}", result);
    return result;
}
