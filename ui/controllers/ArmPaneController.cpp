#ifdef _MSC_VER
#define NOMINMAX
#include <windows.h>
#endif

#include "ArmPaneController.h"
#include "ui_MainWindow.h"
#include "ui/AppController.h"
#include "infra/config/ConfigManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QtConcurrent/QtConcurrent>
#include <cmath>

ArmPaneController::ArmPaneController(Ui::MainWindow* ui, AppController& ctrl,
                                     QObject* parent)
    : QObject(parent)
    , ui_(ui)
    , ctrl_(ctrl)
{
    connect(&arm_connect_watcher_, &QFutureWatcher<bool>::finished,
            this, &ArmPaneController::onArmConnectFinished);
    connect(&arm_move_watcher_, &QFutureWatcher<void>::finished,
            this, &ArmPaneController::onArmMoveFinished);
    connect(&zero_progress_watcher_, &QFutureWatcher<bool>::finished,
            this, &ArmPaneController::onZeroProgressFinished);
}

ArmPaneController::~ArmPaneController() {
    arm_connect_watcher_.waitForFinished();
    arm_move_watcher_.waitForFinished();
    if (zero_progress_dlg_) {
        zero_progress_dlg_->close();
        zero_progress_dlg_->deleteLater();
    }
}

void ArmPaneController::onConnectArm() {
    std::string ip = ui_->armIpEdit->text().toStdString();
    int port = ui_->armPortSpin->value();
    ui_->armToggleButton->setEnabled(false);
    ui_->armStatusLabel->setText(QString::fromUtf8("\xe2\x97\x8f"));
    ui_->armStatusLabel->setProperty("connStatus", "connecting");
    ui_->armStatusLabel->setToolTip(QStringLiteral("连接中..."));
    ui_->armStatusLabel->style()->unpolish(ui_->armStatusLabel);
    ui_->armStatusLabel->style()->polish(ui_->armStatusLabel);
    auto* ctrl = &ctrl_;
    arm_connect_future_ = QtConcurrent::run([ctrl, ip, port]() -> bool {
        return ctrl->connectArm(ip, port);
    });
    arm_connect_watcher_.setFuture(arm_connect_future_);
}

void ArmPaneController::onArmConnectFinished() {
    ui_->armToggleButton->setEnabled(true);
    if (arm_connect_future_.result()) {
        ui_->armToggleButton->setText(QStringLiteral("断开"));
        ui_->armStatusLabel->setText(QString::fromUtf8("\xe2\x97\x8f"));
        ui_->armStatusLabel->setProperty("connStatus", "connected");
        ui_->armStatusLabel->setToolTip(QStringLiteral("已连接"));
        ui_->moveToPosButton->setEnabled(true);
        ui_->readPosButton->setEnabled(true);
        ui_->xPosSpin->setEnabled(true);
        ui_->yPosSpin->setEnabled(true);
        ui_->zPosSpin->setEnabled(true);
        ui_->cellMoveCheck->setEnabled(true);
        ui_->setSpeedBtn->setEnabled(true);
        ui_->speedSpin->setValue(ConfigManager::instance().defaultSpeed());
        ui_->speedSpin->setEnabled(true);
        ui_->aPosSpin->setEnabled(true);
        ui_->bPosSpin->setEnabled(true);
        ui_->contAxisCombo->setEnabled(true);
        ui_->contFwdRadio->setEnabled(true);
        ui_->contRevRadio->setEnabled(true);
        ui_->contMoveBtn->setEnabled(true);
        ui_->emergStopBtn->setEnabled(true);
        ui_->toolbarEmergStopBtn->setEnabled(true);
        ui_->quickScanBtn->setEnabled(true);
        ui_->scanNegativeCheck->setEnabled(true);
        ui_->zeroButton->setEnabled(true);
        emit armConnected();
    } else {
        ui_->armStatusLabel->setText(QString::fromUtf8("\xe2\x97\x8f"));
        ui_->armStatusLabel->setProperty("connStatus", "disconnected");
        ui_->armStatusLabel->setToolTip(QStringLiteral("连接失败"));
    }
    ui_->armStatusLabel->style()->unpolish(ui_->armStatusLabel);
    ui_->armStatusLabel->style()->polish(ui_->armStatusLabel);
}

void ArmPaneController::onDisconnectArm() {
    ctrl_.disconnectArm();
    ui_->armToggleButton->setText(QStringLiteral("连接"));
    ui_->armStatusLabel->setText(QString::fromUtf8("\xe2\x97\x8f"));
    ui_->armStatusLabel->setProperty("connStatus", "disconnected");
    ui_->armStatusLabel->setToolTip(QStringLiteral("未连接"));
    ui_->armStatusLabel->style()->unpolish(ui_->armStatusLabel);
    ui_->armStatusLabel->style()->polish(ui_->armStatusLabel);
    ui_->moveToPosButton->setEnabled(false);
    ui_->readPosButton->setEnabled(false);
    ui_->xPosSpin->setEnabled(false);
    ui_->yPosSpin->setEnabled(false);
    ui_->zPosSpin->setEnabled(false);
    ui_->cellMoveCheck->setEnabled(false);
    ui_->cellMoveCheck->setChecked(false);
    ui_->setSpeedBtn->setEnabled(false);
    ui_->speedSpin->setEnabled(false);
    ui_->aPosSpin->setEnabled(false);
    ui_->bPosSpin->setEnabled(false);
    ui_->contAxisCombo->setEnabled(false);
    ui_->contFwdRadio->setEnabled(false);
    ui_->contRevRadio->setEnabled(false);
    ui_->contMoveBtn->setEnabled(false);
    ui_->emergStopBtn->setEnabled(false);
    ui_->toolbarEmergStopBtn->setEnabled(false);
    ui_->quickScanBtn->setEnabled(false);
    ui_->zeroButton->setEnabled(false);
    emit armDisconnected();
}

void ArmPaneController::onMoveToPosition() {
    ui_->moveToPosButton->setEnabled(false);
    auto* ctrl = &ctrl_;
    int x = static_cast<int>(ui_->xPosSpin->value());
    int y = static_cast<int>(ui_->yPosSpin->value());
    int z = static_cast<int>(ui_->zPosSpin->value());
    arm_move_future_ = QtConcurrent::run([ctrl, x, y, z]() {
        ctrl->armController().moveAxesConcurrent(x, y, z);
    });
    arm_move_watcher_.setFuture(arm_move_future_);
}

void ArmPaneController::onArmMoveFinished() {
    ui_->moveToPosButton->setEnabled(true);
    ui_->zeroButton->setEnabled(true);
    emit armMoveComplete();
}

void ArmPaneController::onReadPosition() {
    auto& arm = ctrl_.armController();
    ui_->xPosSpin->setValue(arm.readPosition(0));
    ui_->yPosSpin->setValue(arm.readPosition(1));
    ui_->zPosSpin->setValue(arm.readPosition(2));
}

void ArmPaneController::onZeroArm() {
    if (!ctrl_.armController().isConnected()) {
        QMessageBox::warning(qobject_cast<QWidget*>(parent()), QStringLiteral("警告"),
                             QStringLiteral("机械臂未连接"));
        return;
    }
    ui_->zeroButton->setEnabled(false);
    auto* ctrl = &ctrl_;
    arm_move_future_ = QtConcurrent::run([ctrl]() {
        ctrl->armController().moveAxesConcurrent(0, 0, 0);
    });
    arm_move_watcher_.setFuture(arm_move_future_);
    ui_->xPosSpin->setValue(0);
    ui_->yPosSpin->setValue(0);
    ui_->zPosSpin->setValue(0);
}

bool ArmPaneController::isArmAtZero() {
    static constexpr double kZeroTolerance = 200.0;
    auto& armCtrl = ctrl_.armController();
    for (int i = 0; i < 5; ++i) {
        double pos = armCtrl.readPosition(i);
        if (std::abs(pos) > kZeroTolerance) {
            return false;
        }
    }
    return true;
}

void ArmPaneController::onZeroProgressFinished() {
    if (zero_progress_dlg_) {
        zero_progress_dlg_->close();
        zero_progress_dlg_->deleteLater();
        zero_progress_dlg_ = nullptr;
    }
    emit zeroSequenceComplete();
}

void ArmPaneController::onArmStatusChanged(const arm::ArmStatus& status) {
    if (status.current_positions.size() >= 5) {
        // Only auto-update when user is NOT editing (avoids overwriting manual input)
        if (!ui_->xPosSpin->hasFocus()) ui_->xPosSpin->setValue(status.current_positions[0]);
        if (!ui_->yPosSpin->hasFocus()) ui_->yPosSpin->setValue(status.current_positions[1]);
        if (!ui_->zPosSpin->hasFocus()) ui_->zPosSpin->setValue(status.current_positions[2]);
        // Update realtime position label
        ui_->posRealtimeLabel->setText(
            QStringLiteral("当前位置: X=%1  Y=%2  Z=%3  A=%4  B=%5")
                .arg(status.current_positions[0])
                .arg(status.current_positions[1])
                .arg(status.current_positions[2])
                .arg(status.current_positions[3])
                .arg(status.current_positions[4]));
    }
    ui_->armStatusLabel->setToolTip(QString::fromStdString(status.status_message));
}
