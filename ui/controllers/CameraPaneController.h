#pragma once

#include <QObject>

class AppController;
namespace Ui { class MainWindow; }

class CameraPaneController : public QObject {
    Q_OBJECT

public:
    explicit CameraPaneController(Ui::MainWindow* ui, AppController& ctrl,
                                  QObject* parent = nullptr);
    ~CameraPaneController() override = default;

public slots:
    void onConnectCamera();
    void onDisconnectCamera();
    void onEnumerateCameras();
    void onCaptureImage();

signals:
    void logMessage(const QString& msg, const QString& level = QStringLiteral("INFO"));
    void cameraConnected();
    void cameraDisconnected();

private:
    Ui::MainWindow* ui_;
    AppController& ctrl_;
};
