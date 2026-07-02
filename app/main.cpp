#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QString>

#include "ui/MainWindow.h"
#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"
#include "infra/log/LogManager.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    a.setApplicationName("ArmSightStitch");
    a.setApplicationVersion("2.0.0");
    a.setOrganizationName("ArmSight");

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
