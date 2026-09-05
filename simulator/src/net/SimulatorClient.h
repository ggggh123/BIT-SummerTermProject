#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <QTcpSocket>

#include "app/SimulatorConfig.h"
#include "core/TelemetryEngine.h"
#include "protocol/FrameCodec.h"

namespace ev::simulator {

// Interface used by SimulatorWindow so the panel can be driven by a fake in tests.
class ISimulatorClient : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isConnected() const = 0;
    virtual void refresh() = 0;
    virtual void sendTelemetry(const QList<TelemetrySample> &samples) = 0;
    virtual void sendFault(const FaultIntent &intent) = 0;

    // R13: the panel reports its real run state so simulator.status stays
    // truthful. The default no-op keeps fake clients in tests source-compatible.
    virtual void setRunning(bool running) { Q_UNUSED(running); }

signals:
    void connected();
    void disconnected();
    void sessionReady();
    void authenticationFailed(const QString &code);
    void chargersReceived(const QList<ChargerSnapshot> &chargers);
    void logMessage(const QString &message);
};

// Framed TCP client: sends simulator.status on connect, publishes telemetry
// and fault events, and keeps a bounded queue while disconnected.
class SimulatorClient : public ISimulatorClient
{
    Q_OBJECT
public:
    SimulatorClient(const SimulatorConfig &config, TelemetryEngine *engine,
                    QObject *parent = nullptr);

    void start() override;
    void stop() override;
    bool isConnected() const override;
    void refresh() override;
    void sendTelemetry(const QList<TelemetrySample> &samples) override;
    void sendFault(const FaultIntent &intent) override;
    void setRunning(bool running) override;

    int queuedSamples() const { return pendingEvents_.size(); }

    static int reconnectDelayMs(int attempt);
    static QString requestIdForSample(const TelemetrySample &sample);

private slots:
    void onConnected();
    void onReadyRead();
    void onErrorOccurred();
    void onDisconnected();
    void onReconnectTimeout();
    void onStatusRefreshTimeout();

private:
    struct PendingEvent
    {
        QString action;
        QJsonObject payload;
        QString requestId;
        bool sent = false;
    };

    void connectToServer();
    void requestStatusRefresh();
    void sendStatus();
    void flushPending();
    void enqueueEvent(const QString &action, const QJsonObject &payload,
                      const QString &requestId);
    void acknowledgeEvent(const QString &requestId);
    void sendRequest(const QString &action, const QJsonObject &payload,
                     const QString &requestId);
    void handleResponse(const QByteArray &json);

    SimulatorConfig config_;
    TelemetryEngine *engine_;
    QTcpSocket socket_;
    QTimer reconnectTimer_;
    QTimer statusRefreshTimer_;
    ev::protocol::FrameDecoder decoder_;
    int reconnectAttempt_ = 0;
    int eventCount_ = 0;
    bool stopping_ = false;
    bool running_ = false;
    bool sessionReady_ = false;
    QString pendingStatusRequestId_;
    bool statusRefreshQueued_ = false;
    quint64 statusRequestSequence_ = 0;
    QQueue<PendingEvent> pendingEvents_;
};

} // namespace ev::simulator
