#include "net/SimulatorClient.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "contracts/Actions.h"
#include "protocol/JsonEnvelope.h"

namespace ev::simulator {

namespace {

QString isoPlus08(const QDateTime &dt)
{
    return dt.toOffsetFromUtc(8 * 3600).toString(Qt::ISODate);
}

} // namespace

SimulatorClient::SimulatorClient(const SimulatorConfig &config,
                                 TelemetryEngine *engine, QObject *parent)
    : ISimulatorClient(parent),
      config_(config),
      engine_(engine)
{
    connect(&socket_, &QTcpSocket::connected, this, &SimulatorClient::onConnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &SimulatorClient::onReadyRead);
    connect(&socket_, &QTcpSocket::disconnected, this, &SimulatorClient::onDisconnected);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &SimulatorClient::onErrorOccurred);
    connect(&reconnectTimer_, &QTimer::timeout, this, &SimulatorClient::onReconnectTimeout);
}

void SimulatorClient::start()
{
    stopping_ = false;
    connectToServer();
}

void SimulatorClient::stop()
{
    stopping_ = true;
    reconnectTimer_.stop();
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
    emit connected();
    sendStatus();
    flushPending();
}

void SimulatorClient::setRunning(bool running)
{
    running_ = running;
    // Push the state change immediately so the server does not keep a stale
    // "running" heartbeat after the panel pauses (R13).
    if (socket_.state() == QAbstractSocket::ConnectedState)
        sendStatus();
}

void SimulatorClient::sendStatus()
{
    QJsonObject payload;
    payload[QStringLiteral("state")] =
        running_ ? QStringLiteral("running") : QStringLiteral("paused");
    payload[QStringLiteral("simulatedAt")] = isoPlus08(engine_->currentTime());
    payload[QStringLiteral("eventCount")] = eventCount_;

    pendingStatusRequestId_ = QStringLiteral("sim-status-%1")
                                  .arg(engine_->currentTime().toMSecsSinceEpoch());
    sendRequest(ev::actions::SimulatorStatus, payload, pendingStatusRequestId_);
}

void SimulatorClient::refresh()
{
    if (isConnected())
        sendStatus();
    else
        emit logMessage(QStringLiteral("refresh: not connected"));
}

void SimulatorClient::sendTelemetry(const QList<TelemetrySample> &samples)
{
    for (const TelemetrySample &s : samples) {
        if (isConnected()) {
            QJsonObject payload;
            payload[QStringLiteral("chargerId")] = s.chargerId;
            payload[QStringLiteral("recordedAt")] = isoPlus08(s.recordedAt);
            payload[QStringLiteral("powerKw")] = s.powerKw;
            payload[QStringLiteral("energyIncrementKwh")] = s.energyIncrementKwh;
            payload[QStringLiteral("status")] = s.status;
            sendRequest(ev::actions::TelemetryPush, payload,
                        requestIdForSample(s));
            ++eventCount_;
        } else {
            if (pendingSamples_.size() >= config_.maxQueueSamples)
                pendingSamples_.dequeue();  // drop oldest
            pendingSamples_.enqueue(s);
        }
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
    if (isConnected()) {
        sendRequest(ev::actions::SimulatorFaultSet, payload, requestId);
        ++eventCount_;
    } else {
        emit logMessage(QStringLiteral("fault event dropped: not connected"));
    }
}

void SimulatorClient::flushPending()
{
    while (!pendingSamples_.isEmpty()) {
        const TelemetrySample s = pendingSamples_.dequeue();
        QJsonObject payload;
        payload[QStringLiteral("chargerId")] = s.chargerId;
        payload[QStringLiteral("recordedAt")] = isoPlus08(s.recordedAt);
        payload[QStringLiteral("powerKw")] = s.powerKw;
        payload[QStringLiteral("energyIncrementKwh")] = s.energyIncrementKwh;
        payload[QStringLiteral("status")] = s.status;
        sendRequest(ev::actions::TelemetryPush, payload, requestIdForSample(s));
        ++eventCount_;
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

    if (response.requestId == pendingStatusRequestId_ && response.ok
        && response.data.isObject()) {
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
        }
        pendingStatusRequestId_.clear();
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
    emit disconnected();
    if (!stopping_ && !reconnectTimer_.isActive())
        reconnectTimer_.start(reconnectDelayMs(reconnectAttempt_++));
}

void SimulatorClient::onReconnectTimeout()
{
    if (!stopping_)
        connectToServer();
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
