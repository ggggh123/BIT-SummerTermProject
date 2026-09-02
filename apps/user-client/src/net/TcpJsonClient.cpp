#include "net/TcpJsonClient.h"

#include "protocol/JsonEnvelope.h"

#include <QTimer>
#include <QTcpSocket>
#include <QUuid>

namespace {

const QString kNotConnected = QStringLiteral("NOT_CONNECTED");
const QString kTransportError = QStringLiteral("TRANSPORT_ERROR");
const QString kProtocolError = QStringLiteral("PROTOCOL_ERROR");
const QString kTimeout = QStringLiteral("TIMEOUT");
constexpr int kResponseTimeoutMs = 10'000;
constexpr int kMaxReconnectAttempt = 2;

} // namespace

TcpJsonClient::TcpJsonClient(QObject *parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , reconnectTimer_(new QTimer(this))
{
    reconnectTimer_->setSingleShot(true);

    connect(socket_, &QTcpSocket::connected, this, [this] {
        lossHandled_ = false;
        reconnectAttempt_ = 0;
        if (!connectionReported_) {
            connectionReported_ = true;
            emit connectionChanged(true);
        }

        const auto requestIds = pendingRequests_.keys();
        for (const QString &requestId : requestIds) {
            const auto it = pendingRequests_.constFind(requestId);
            if (it != pendingRequests_.cend() && it->awaitingReplay) {
                writeRequest(requestId);
            }
        }
    });

    connect(socket_, &QTcpSocket::readyRead, this, [this] {
        try {
            const QList<QByteArray> frames = decoder_.append(socket_->readAll());
            for (const QByteArray &frame : frames) {
                const ev::protocol::ResponseEnvelope response = ev::protocol::parseResponse(frame);
                auto it = pendingRequests_.find(response.requestId);
                if (it == pendingRequests_.end()) {
                    continue;
                }

                QTimer *timer = it->timeoutTimer;
                pendingRequests_.erase(it);
                timer->stop();
                timer->deleteLater();
                emit responseReceived(response);
            }
        } catch (const ev::protocol::FrameError &error) {
            protocolFailure(QString::fromUtf8(error.what()));
        } catch (const ev::protocol::EnvelopeError &error) {
            protocolFailure(error.message());
        }
    });

    connect(socket_, &QTcpSocket::disconnected, this, [this] {
        handleTransportLoss();
    });

    connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (socket_->state() == QAbstractSocket::UnconnectedState) {
            handleTransportLoss();
        }
    });

    connect(reconnectTimer_, &QTimer::timeout, this, [this] {
        if (!manualDisconnect_ && !host_.isEmpty() && port_ != 0) {
            lossHandled_ = false;
            socket_->connectToHost(host_, port_);
        }
    });
}

void TcpJsonClient::configure(QString host, quint16 port)
{
    host_ = std::move(host);
    port_ = port;
}

QString TcpJsonClient::send(QString action, QJsonObject payload, QString token)
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray frame;
    try {
        frame = ev::protocol::encodeFrame(ev::protocol::toJson({1, requestId, action, token, payload}));
    } catch (const std::exception &error) {
        QTimer::singleShot(0, this, [this, requestId, message = QString::fromUtf8(error.what())] {
            emit transportFailed(requestId, kProtocolError, message);
        });
        return requestId;
    }

    if (socket_->state() != QAbstractSocket::ConnectedState) {
        QTimer::singleShot(0, this, [this, requestId] {
            emit transportFailed(
                requestId, kNotConnected, QStringLiteral("not connected to the server"));
        });
        return requestId;
    }

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, requestId] {
        onRequestTimeout(requestId);
    });
    pendingRequests_.insert(requestId, {std::move(action), std::move(frame), timer});
    writeRequest(requestId);
    return requestId;
}

void TcpJsonClient::cancelRequest(const QString &requestId)
{
    auto it = pendingRequests_.find(requestId);
    if (it == pendingRequests_.end()) {
        return;
    }
    QTimer *timer = it->timeoutTimer;
    pendingRequests_.erase(it);
    timer->stop();
    timer->deleteLater();
}

void TcpJsonClient::connectToServer()
{
    manualDisconnect_ = false;
    reconnectTimer_->stop();
    if (host_.isEmpty() || port_ == 0 || socket_->state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    socket_->connectToHost(host_, port_);
}

void TcpJsonClient::disconnectFromServer()
{
    manualDisconnect_ = true;
    reconnectTimer_->stop();
    decoder_.reset();
    if (socket_->state() == QAbstractSocket::UnconnectedState) {
        failAll(kTransportError, QStringLiteral("connection closed by client"));
        return;
    }
    socket_->disconnectFromHost();
}

void TcpJsonClient::writeRequest(const QString &requestId)
{
    auto it = pendingRequests_.find(requestId);
    if (it == pendingRequests_.end() || socket_->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const qint64 bytesWritten = writeOverrideForTest_
        ? writeOverrideForTest_(it->frame)
        : socket_->write(it->frame);
    if (bytesWritten != it->frame.size()) {
        socket_->abort();
        failRequest(requestId, kTransportError, QStringLiteral("could not write request frame"));
        return;
    }

    it->written = true;
    it->awaitingReplay = false;
    startTimeout(requestId);
}

void TcpJsonClient::startTimeout(const QString &requestId)
{
    const auto it = pendingRequests_.constFind(requestId);
    if (it != pendingRequests_.cend()) {
        it->timeoutTimer->start(kResponseTimeoutMs);
    }
}

void TcpJsonClient::failRequest(const QString &requestId, const QString &code, const QString &message)
{
    auto it = pendingRequests_.find(requestId);
    if (it == pendingRequests_.end()) {
        return;
    }

    QTimer *timer = it->timeoutTimer;
    pendingRequests_.erase(it);
    timer->stop();
    timer->deleteLater();
    emit transportFailed(requestId, code, message);
}

void TcpJsonClient::failAll(const QString &code, const QString &message)
{
    const auto requestIds = pendingRequests_.keys();
    for (const QString &requestId : requestIds) {
        failRequest(requestId, code, message);
    }
}

void TcpJsonClient::handleTransportLoss()
{
    if (lossHandled_) {
        return;
    }
    lossHandled_ = true;
    decoder_.reset();

    if (connectionReported_) {
        connectionReported_ = false;
        emit connectionChanged(false);
    }

    const bool unexpected = !manualDisconnect_;
    const auto requestIds = pendingRequests_.keys();
    for (const QString &requestId : requestIds) {
        auto it = pendingRequests_.find(requestId);
        if (it == pendingRequests_.end()) {
            continue;
        }
        if (unexpected && it->written && !it->replayed && isSafeReadAction(it->action)) {
            it->timeoutTimer->stop();
            it->replayed = true;
            it->written = false;
            it->awaitingReplay = true;
        } else {
            failRequest(requestId, kTransportError, QStringLiteral("connection lost before response"));
        }
    }

    if (unexpected) {
        scheduleReconnect();
    }
}

void TcpJsonClient::scheduleReconnect()
{
    if (manualDisconnect_ || host_.isEmpty() || port_ == 0 || reconnectTimer_->isActive()) {
        return;
    }

    const int delayMs = 1'000 * (1 << reconnectAttempt_);
    reconnectAttempt_ = qMin(reconnectAttempt_ + 1, kMaxReconnectAttempt);
    reconnectTimer_->start(delayMs);
}

void TcpJsonClient::protocolFailure(const QString &message)
{
    decoder_.reset();
    failAll(kProtocolError, message);
    socket_->abort();
}

void TcpJsonClient::onRequestTimeout(const QString &requestId)
{
    failRequest(requestId, kTimeout, QStringLiteral("response timed out"));
}

bool TcpJsonClient::isSafeReadAction(const QString &action) const
{
    return action == QStringLiteral("system.health")
        || action == QStringLiteral("station.list")
        || action == QStringLiteral("station.detail")
        || action == QStringLiteral("charger.list")
        || action == QStringLiteral("order.current")
        || action == QStringLiteral("order.list")
        || action == QStringLiteral("forecast.latest");
}
