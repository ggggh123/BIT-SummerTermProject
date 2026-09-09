#pragma once

#include <QObject>
#include <QString>

namespace ev::simulator {

class ISimulatorClient;

// 启动器状态文件：用原子覆盖把 starting/waiting_auth/ready/auth_failed/stopped
// 写到 EV_SIMULATOR_STATUS_FILE 指定的本地 JSON，供启动脚本判断进程是否就绪。
class RuntimeStatusWriter : public QObject
{
    Q_OBJECT
public:
    RuntimeStatusWriter(const QString &filePath, ISimulatorClient *client,
                        QObject *parent = nullptr);

    QString filePath() const { return filePath_; }
    bool start(QString *errorMessage = nullptr);
    bool stop(QString *errorMessage = nullptr);

signals:
    void writeFailed(const QString &message);

private:
    bool writeState(const QString &sessionState, QString *errorMessage);
    void transitionTo(const QString &sessionState);

    QString filePath_;
    bool hasPublished_ = false;
};

} // namespace ev::simulator
