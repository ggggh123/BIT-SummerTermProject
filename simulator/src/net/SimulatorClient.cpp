#include "net/SimulatorClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include "contracts/Actions.h"
#include "protocol/JsonEnvelope.h"

namespace ev::simulator {

namespace {

QString isoPlus08(const QDateTime &dt)
{
    return dt.toOffsetFromUtc(8 * 3600).toString(Qt::ISODateWithMs);
}

} // namespace

SimulatorClient::SimulatorClient(const SimulatorConfig &config,
                                 TelemetryEngine *engine, QObject *parent)
    : ISimulatorClient(parent),
      config_(config),
      engine_(engine),
      instanceId_(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    connect(&socket_, &QTcpSocket::connected, this, &SimulatorClient::onConnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &SimulatorClient::onReadyRead);
    connect(&socket_, &QTcpSocket::disconnected, this, &SimulatorClient::onDisconnected);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &SimulatorClient::onErrorOccurred);
    connect(&reconnectTimer_, &QTimer::timeout, this, &SimulatorClient::onReconnectTimeout);
    statusRefreshTimer_.setInterval(qBound(1000, config_.intervalMs, 10000));
    connect(&statusRefreshTimer_, &QTimer::timeout,
            this, &SimulatorClient::onStatusRefreshTimeout);
}

void SimulatorClient::start()
{
    stopping_ = false;
    if (config_.token.trimmed().isEmpty())
        emit logMessage(QStringLiteral("simulator authentication token is empty"));
    connectToServer();
}

void SimulatorClient::stop()
{
    stopping_ = true;
    reconnectTimer_.stop();
    statusRefreshTimer_.stop();
    if (socket_.state() != QAbstractSocket::UnconnectedState)
        socket_.disconnectFromHost();
}

bool SimulatorClient::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void SimulatorClient::connectToServer()
{
    if (socket_.state() != QAbstractSocket::UnconnectedState)
        socket_.abort();
    socket_.connectToHost(config_.host, config_.port);
}

void SimulatorClient::onConnected()
{
    reconnectAttempt_ = 0;
    reconnectTimer_.stop();
    sessionReady_ = false;
    emit connected();
    statusRefreshTimer_.start();
    sendStatus();
}

void SimulatorClient::setRunning(bool running)
{
    running_ = running;
    // Push the state change immediately so the server does not keep a stale
    // "running" heartbeat after the panel pauses (R13).
    requestStatusRefresh();
}

void SimulatorClient::requestStatusRefresh()
{
    if (!isConnected())
        return;
    if (!pendingStatusRequestId_.isEmpty()) {
        statusRefreshQueued_ = true;
        return;
    }
    sendStatus();
}

void SimulatorClient::sendStatus()
{
    QJsonObject payload;
    payload[QStringLiteral("state")] =
        running_ ? QStringLiteral("running") : QStringLiteral("paused");
    payload[QStringLiteral("simulatedAt")] = isoPlus08(engine_->currentTime());
    payload[QStringLiteral("eventCount")] = eventCount_;

    pendingStatusRequestId_ = QStringLiteral("sim-status-%1-%2-%3")
                                  .arg(instanceId_)
                                  .arg(engine_->currentTime().toMSecsSinceEpoch())
                                  .arg(++statusRequestSequence_);
    sendRequest(ev::actions::SimulatorStatus, payload, pendingStatusRequestId_);
}

void SimulatorClient::refresh()
{
    if (isConnected())
        requestStatusRefresh();
    else
        emit logMessage(QStringLiteral("refresh: not connected"));
}

void SimulatorClient::sendTelemetry(const QList<TelemetrySample> &samples)
{
    for (const TelemetrySample &s : samples) {
        QJsonObject payload;
        payload[QStringLiteral("chargerId")] = s.chargerId;
        payload[QStringLiteral("recordedAt")] = isoPlus08(s.recordedAt);
        payload[QStringLiteral("powerKw")] = s.powerKw;
        payload[QStringLiteral("energyIncrementKwh")] = s.energyIncrementKwh;
        payload[QStringLiteral("status")] = s.status;
        enqueueEvent(ev::actions::TelemetryPush, payload, requestIdForSample(s));
    }
}

void SimulatorClient::sendFault(const FaultIntent &intent)
{
    QJsonObject payload;
    payload[QStringLiteral("chargerId")] = intent.chargerId;
    payload[QStringLiteral("fault")] = intent.fault;
    payload[QStringLiteral("recordedAt")] = isoPlus08(intent.recordedAt);

    const QString requestId = QStringLiteral("fault-%1-%2")
        .arg(intent.chargerId)
        .arg(intent.recordedAt.toMSecsSinceEpoch());
    enqueueEvent(ev::actions::SimulatorFaultSet, payload, requestId);
}

void SimulatorClient::flushPending()
{
    if (!isConnected() || !sessionReady_)
        return;
    for (PendingEvent &event : pendingEvents_) {
        if (event.sent)
            continue;
        event.sent = true;
        sendRequest(event.action, event.payload, event.requestId);
    }
}

void SimulatorClient::enqueueEvent(const QString &action,
                                   const QJsonObject &payload,
                                   const QString &requestId)
{
    const int limit = qMax(1, config_.maxQueueSamples);
    if (pendingEvents_.size() >= limit) {
        const PendingEvent dropped = pendingEvents_.dequeue();
        emit logMessage(QStringLiteral("event queue full, dropped %1")
                            .arg(dropped.requestId));
    }
    pendingEvents_.enqueue({action, payload, requestId, false});
    ++eventCount_;
    flushPending();
}

void SimulatorClient::acknowledgeEvent(const QString &requestId)
{
    for (int i = 0; i < pendingEvents_.size(); ++i) {
        if (pendingEvents_.at(i).requestId == requestId) {
            pendingEvents_.removeAt(i);
            return;
        }
    }
}

void SimulatorClient::sendRequest(const QString &action,
                                  const QJsonObject &payload,
                                  const QString &requestId)
{
    ev::protocol::RequestEnvelope request;
    request.version = 1;
    request.requestId = requestId;
    request.action = action;
    request.token = config_.token;
    request.payload = payload;
    socket_.write(ev::protocol::encodeFrame(ev::protocol::toJson(request)));
}

void SimulatorClient::onReadyRead()
{
    QList<QByteArray> frames;
    try {
        frames = decoder_.append(socket_.readAll());
    } catch (const ev::protocol::FrameError &e) {
        emit logMessage(QStringLiteral("frame error: %1").arg(QString::fromLatin1(e.what())));
        socket_.abort();
        return;
    }
    for (const QByteArray &frame : frames)
        handleResponse(frame);
}

void SimulatorClient::handleResponse(const QByteArray &json)
{
    ev::protocol::ResponseEnvelope response;
    try {
        response = ev::protocol::parseResponse(json);
    } catch (const ev::protocol::EnvelopeError &e) {
        emit logMessage(QStringLiteral("bad response: %1").arg(e.message()));
        return;
    }

    emit logMessage(QStringLiteral("response %1 %2")
                        .arg(response.requestId, response.code));

    const bool authenticationRejected =
        response.code == QLatin1String("AUTH_REQUIRED")
        || response.code == QLatin1String("FORBIDDEN");
    if (authenticationRejected) {
        sessionReady_ = false;
        statusRefreshTimer_.stop();
        statusRefreshQueued_ = false;
        emit authenticationFailed(response.code);
    }

    bool deviceEventResponse = false;
    for (const PendingEvent &event : pendingEvents_) {
        if (event.requestId == response.requestId) {
            deviceEventResponse = true;
            break;
        }
    }
    acknowledgeEvent(response.requestId);

    if (response.requestId == pendingStatusRequestId_) {
        pendingStatusRequestId_.clear();
        if (response.ok && response.data.isObject()) {
            const QJsonObject data = response.data.toObject();
            const QJsonValue chargersValue = data.value(QStringLiteral("chargers"));
            if (chargersValue.isArray()) {
                QList<ChargerSnapshot> chargers;
                for (const QJsonValue &v : chargersValue.toArray()) {
                    const QJsonObject c = v.toObject();
                    ChargerSnapshot snapshot;
                    snapshot.chargerId = c.value(QStringLiteral("chargerId")).toInt();
                    snapshot.status = c.value(QStringLiteral("status")).toString();
                    snapshot.powerKw = c.value(QStringLiteral("powerKw")).toDouble();
                    chargers.append(snapshot);
                }
                engine_->replaceChargers(chargers);
                emit chargersReceived(chargers);
                sessionReady_ = true;
                emit sessionReady();
                flushPending();
            } else {
                emit logMessage(QStringLiteral(
                    "invalid simulator.status response: chargers snapshot missing"));
            }
        }
        if (statusRefreshQueued_ && !authenticationRejected) {
            statusRefreshQueued_ = false;
            sendStatus();
        }
    }

    if (deviceEventResponse
        && response.code == QLatin1String("ORDER_STATE_CONFLICT")) {
        requestStatusRefresh();
    }
}

void SimulatorClient::onErrorOccurred()
{
    if (!stopping_ && socket_.state() == QAbstractSocket::UnconnectedState) {
        emit logMessage(QStringLiteral("connection error: %1").arg(socket_.errorString()));
        if (!reconnectTimer_.isActive())
            reconnectTimer_.start(reconnectDelayMs(reconnectAttempt_++));
    }
}

void SimulatorClient::onDisconnected()
{
    decoder_.reset();
    statusRefreshTimer_.stop();
    sessionReady_ = false;
    pendingStatusRequestId_.clear();
    statusRefreshQueued_ = false;
    for (PendingEvent &event : pendingEvents_)
        event.sent = false;
    emit disconnected();
    if (!stopping_ && !reconnectTimer_.isActive())
        reconnectTimer_.start(reconnectDelayMs(reconnectAttempt_++));
}

void SimulatorClient::onReconnectTimeout()
{
    if (!stopping_)
        connectToServer();
}

void SimulatorClient::onStatusRefreshTimeout()
{
    requestStatusRefresh();
}

int SimulatorClient::reconnectDelayMs(int attempt)
{
    if (attempt <= 0)
        return 1000;
    if (attempt == 1)
        return 2000;
    return 4000;
}

QString SimulatorClient::requestIdForSample(const TelemetrySample &sample)
{
    return QStringLiteral("telemetry-%1-%2")
        .arg(sample.chargerId)
        .arg(sample.recordedAt.toMSecsSinceEpoch());
}

} // namespace ev::simulator
