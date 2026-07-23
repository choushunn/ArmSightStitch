#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStandardPaths>

#include "version.h"  // auto-generated from git tags
#include "ui/MainWindow.h"
#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"
#include "infra/log/LogManager.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    a.setApplicationName("明场显微成像验证组件");
    a.setApplicationVersion(APP_VERSION);
    a.setOrganizationName("");

    // Load configuration from JSON file if available
    auto& cfg = ConfigManager::instance();
    QString configPath = QApplication::applicationDirPath() + "/config.json";
    if (QFileInfo::exists(configPath)) {
        cfg.loadFromFile(configPath.toStdString());
        // 旧默认值迁移：stitch 0→3(SeamFeather), detection 1→2(Sobel)
        if (cfg.stitchAlgorithm() == 0) cfg.setStitchAlgorithm(3);
        if (cfg.detectionAlgorithm() == 1) cfg.setDetectionAlgorithm(2);
    }

    // Initialize spdlog logging
    std::string logPath = cfg.logPath();
    if (!logPath.empty()) {
        QDir().mkpath(QFileInfo(QString::fromStdString(logPath)).absolutePath());
    }
    infra::LogManager::init(logPath);
    SPDLOG_INFO("[App] Application started");

    if (QFileInfo::exists(configPath)) {
        SPDLOG_INFO("[App] Configuration loaded from {}", configPath.toStdString());
    } else {
        SPDLOG_INFO("[App] No config file found, using defaults");
    }

    // ── Ensure Z-map / Scale-map templates exist in Documents ──
    // Generate inline if missing — no dependency on external template files.
    auto ensureJsonFile = [](const std::string& filePath, const QString& jsonContent,
                              const std::string& label) {
        QString targetPath = QString::fromStdString(filePath);
        if (!targetPath.isEmpty() && !QFileInfo::exists(targetPath)) {
            QDir().mkpath(QFileInfo(targetPath).absolutePath());
            QFile f(targetPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(jsonContent.toUtf8());
                f.close();
                SPDLOG_INFO("[App] {} template created at {}", label, filePath);
            } else {
                SPDLOG_WARN("[App] Failed to create {} template at {}", label, filePath);
            }
        }
    };
    ensureJsonFile(cfg.zMapFile(), QStringLiteral(
        "{\n"
        "  \"description\": \"Z-Map — 每个网格位置的 Z 轴高度（脉冲数）\",\n"
        "  \"z_values\": [\n"
        "    [80000,80000,80000,80000,80000,80000,80000,80000,80000,80000],\n"
        "    [80000,79000,78500,78200,78000,78000,78200,78500,79000,80000],\n"
        "    [80000,78500,77500,76500,76000,76000,76500,77500,78500,80000],\n"
        "    [80000,78200,76500,75000,74000,74000,75000,76500,78200,80000],\n"
        "    [80000,78000,76000,74000,72000,72000,74000,76000,78000,80000],\n"
        "    [80000,78000,76000,74000,72000,72000,74000,76000,78000,80000],\n"
        "    [80000,78200,76500,75000,74000,74000,75000,76500,78200,80000],\n"
        "    [80000,78500,77500,76500,76000,76000,76500,77500,78500,80000],\n"
        "    [80000,79000,78500,78200,78000,78000,78200,78500,79000,80000],\n"
        "    [80000,80000,80000,80000,80000,80000,80000,80000,80000,80000]\n"
        "  ]\n"
        "}\n"), "Z-map");
    ensureJsonFile(cfg.zRadialFile(), QStringLiteral(
        "{\n"
        "  \"description\": \"Radial Z-Map — 按归一化半径采样的 Z 轴高度（脉冲数）\",\n"
        "  \"z_radial\": [\n"
        "    {\"r\":0.0,\"z\":80000},\n"
        "    {\"r\":0.5,\"z\":78000},\n"
        "    {\"r\":1.0,\"z\":72000}\n"
        "  ]\n"
        "}\n"), "Radial Z-map");
    ensureJsonFile(cfg.scaleMapFile(), QStringLiteral(
        "{\n"
        "  \"description\": \"Scale-Map — 每个网格位置的缩放因子\",\n"
        "  \"scale_values\": [\n"
        "    [1.00,1.00,1.00,1.00,1.00,1.00,1.00,1.00,1.00,1.00],\n"
        "    [1.00,0.99,0.99,0.98,0.98,0.98,0.98,0.99,0.99,1.00],\n"
        "    [1.00,0.99,0.98,0.97,0.96,0.96,0.97,0.98,0.99,1.00],\n"
        "    [1.00,0.98,0.97,0.95,0.94,0.94,0.95,0.97,0.98,1.00],\n"
        "    [1.00,0.98,0.96,0.94,0.93,0.93,0.94,0.96,0.98,1.00],\n"
        "    [1.00,0.98,0.96,0.94,0.93,0.93,0.94,0.96,0.98,1.00],\n"
        "    [1.00,0.98,0.97,0.95,0.94,0.94,0.95,0.97,0.98,1.00],\n"
        "    [1.00,0.99,0.98,0.97,0.96,0.96,0.97,0.98,0.99,1.00],\n"
        "    [1.00,0.99,0.99,0.98,0.98,0.98,0.98,0.99,0.99,1.00],\n"
        "    [1.00,1.00,1.00,1.00,1.00,1.00,1.00,1.00,1.00,1.00]\n"
        "  ]\n"
        "}\n"), "Scale-map");

    ensureJsonFile(cfg.cropOffsetFile(), QStringLiteral(
        "{\n"
        "  \"description\": \"Crop Offset Map — 每个网格位置的裁剪偏移量(像素)\",\n"
        "  \"ox_values\": [\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0]\n"
        "  ],\n"
        "  \"oy_values\": [\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0],\n"
        "    [0,0,0,0,0,0,0,0,0,0]\n"
        "  ]\n"
        "}\n"), "Crop-offset map");

    // Create application controller and main window (MVP)
    AppController ctrl;
    MainWindow w(ctrl);
    w.resize(1728, 972);   // 1920*0.9 × 1080*0.9
    w.showFullScreen();

    SPDLOG_INFO("[App] Application entering event loop");
    int result = a.exec();
    SPDLOG_INFO("[App] Application exited with code {}", result);
    return result;
}
