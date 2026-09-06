#include "app/RuntimeStatusWriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "net/SimulatorClient.h"

namespace ev::simulator {

RuntimeStatusWriter::RuntimeStatusWriter(const QString &filePath,
                                         ISimulatorClient *client,
                                         QObject *parent)
    : QObject(parent), filePath_(filePath)
{
    if (!client)
        return;

    connect(client, &ISimulatorClient::connected, this, [this]() {
        transitionTo(QStringLiteral("waiting_auth"));
    });
    connect(client, &ISimulatorClient::sessionReady, this, [this]() {
        transitionTo(QStringLiteral("ready"));
    });
    connect(client, &ISimulatorClient::authenticationFailed, this,
            [this](const QString &) {
                transitionTo(QStringLiteral("auth_failed"));
            });
    connect(client, &ISimulatorClient::disconnected, this, [this]() {
        transitionTo(QStringLiteral("disconnected"));
    });
}

bool RuntimeStatusWriter::start(QString *errorMessage)
{
    return writeState(QStringLiteral("starting"), errorMessage);
}

bool RuntimeStatusWriter::stop(QString *errorMessage)
{
    QString localError;
    const bool written = writeState(QStringLiteral("stopped"), &localError);
    if (!written)
        emit writeFailed(localError);
    if (errorMessage)
        *errorMessage = localError;
    return written;
}

bool RuntimeStatusWriter::writeState(const QString &sessionState,
                                     QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    if (filePath_.isEmpty())
        return true;

    QJsonObject status;
    status[QStringLiteral("schemaVersion")] = 1;
    status[QStringLiteral("pid")] = QCoreApplication::applicationPid();
    status[QStringLiteral("sessionState")] = sessionState;
    status[QStringLiteral("updatedAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QByteArray contents = QJsonDocument(status).toJson(QJsonDocument::Compact);

    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        const QString message = QStringLiteral("无法打开运行状态文件 %1：%2")
                                    .arg(filePath_, file.errorString());
        if (errorMessage)
            *errorMessage = message;
        return false;
    }
    if (file.write(contents) != contents.size()) {
        const QString message = QStringLiteral("无法写入运行状态文件 %1：%2")
                                    .arg(filePath_, file.errorString());
        file.cancelWriting();
        if (errorMessage)
            *errorMessage = message;
        return false;
    }
    if (!file.commit()) {
        const QString message = QStringLiteral("无法提交运行状态文件 %1：%2")
                                    .arg(filePath_, file.errorString());
        if (errorMessage)
            *errorMessage = message;
        return false;
    }
    return true;
}

void RuntimeStatusWriter::transitionTo(const QString &sessionState)
{
    QString error;
    if (!writeState(sessionState, &error))
        emit writeFailed(error);
}

} // namespace ev::simulator
