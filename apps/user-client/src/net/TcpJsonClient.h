#pragma once

#include "protocol/Envelope.h"
#include "protocol/FrameCodec.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class QTcpSocket;
class QTimer;

class TcpJsonClient final : public QObject
{
    Q_OBJECT

public:
    explicit TcpJsonClient(QObject *parent = nullptr);

    void configure(QString host, quint16 port);
    QString send(QString action, QJsonObject payload, QString token = {});
    void cancelRequest(const QString &requestId);
    void connectToServer();
    void disconnectFromServer();

signals:
    void responseReceived(ev::protocol::ResponseEnvelope response);
    void transportFailed(QString requestId, QString code, QString message);
    void connectionChanged(bool connected);

private:
    friend class TcpJsonClientTest;

    struct PendingRequest {
        QString action;
        QByteArray frame;
        QTimer *timeoutTimer = nullptr;
        bool written = false;
        bool replayed = false;
        bool awaitingReplay = false;
    };

    void writeRequest(const QString &requestId);
    void startTimeout(const QString &requestId);
    void failRequest(const QString &requestId, const QString &code, const QString &message);
    void failAll(const QString &code, const QString &message);
    void handleTransportLoss();
    void scheduleReconnect();
    void protocolFailure(const QString &message);
    void onRequestTimeout(const QString &requestId);
    bool isSafeReadAction(const QString &action) const;

    QTcpSocket *socket_;
    QTimer *reconnectTimer_;
    QString host_;
    quint16 port_ = 0;
    QHash<QString, PendingRequest> pendingRequests_;
    ev::protocol::FrameDecoder decoder_;
    int reconnectAttempt_ = 0;
    bool manualDisconnect_ = false;
    bool connectionReported_ = false;
    bool lossHandled_ = false;
    // QTcpSocket normally accepts a complete frame into its write buffer; this seam models a short write.
    std::function<qint64(const QByteArray &)> writeOverrideForTest_;
};
