#pragma once

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QProgressDialog>

#include "core/arm/ModbusArmController.h"

class AppController;
namespace Ui { class MainWindow; }

class ArmPaneController : public QObject {
    Q_OBJECT

public:
    explicit ArmPaneController(Ui::MainWindow* ui, AppController& ctrl,
                               QObject* parent = nullptr);
    ~ArmPaneController() override;

public slots:
    void onConnectArm();
    void onDisconnectArm();
    void onArmConnectFinished();
    void onMoveToPosition();
    void onArmMoveFinished();
    void onReadPosition();
    void onZeroArm();
    void onZeroProgressFinished();
    void onArmStatusChanged(const arm::ArmStatus& status);

signals:
    void logMessage(const QString& msg, const QString& level = QStringLiteral("INFO"));
    void armConnected();
    void armDisconnected();
    void zeroSequenceComplete();
    void armMoveComplete();

private:
    bool isArmAtZero();

    Ui::MainWindow* ui_;
    AppController& ctrl_;

    QFuture<bool> arm_connect_future_;
    QFutureWatcher<bool> arm_connect_watcher_;
    QFuture<void> arm_move_future_;
    QFutureWatcher<void> arm_move_watcher_;
    QFuture<bool> zero_progress_future_;
    QFutureWatcher<bool> zero_progress_watcher_;
    QProgressDialog* zero_progress_dlg_ = nullptr;
};
