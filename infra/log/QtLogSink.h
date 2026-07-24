#pragma once

#include <spdlog/sinks/base_sink.h>
#include <QObject>
#include <QString>
#include <mutex>

namespace infra {

/// Custom spdlog sink that emits log messages as Qt signals.
/// Connect to logMessage() from the UI thread to display logs in real time.
class QtLogSink : public QObject,
                  public spdlog::sinks::base_sink<std::mutex> {
    Q_OBJECT

public:
    explicit QtLogSink(QObject* parent = nullptr) : QObject(parent) {}

signals:
    /// Emitted on every log entry. Thread-safe — use Qt::QueuedConnection.
    void logMessage(const QString& msg, int level);

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);
        QString text = QString::fromUtf8(formatted.data(), static_cast<int>(formatted.size()));
        emit logMessage(text, static_cast<int>(msg.level));
    }

    void flush_() override {}
};

} // namespace infra
