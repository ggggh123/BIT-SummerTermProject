#pragma once

#include <QObject>
#include <QString>

namespace ev::simulator {

class ISimulatorClient;

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
