#include "network/ApiServer.h"

#include "contracts/Actions.h"
#include "protocol/JsonEnvelope.h"

#include <QJsonObject>
#include <QTcpSocket>

ApiServer::ApiServer(AuthService *authService, DashboardService *dashboardService, ForecastService *forecastService, QObject *parent)
    : QTcpServer(parent)
    , m_authService(authService)
    , m_dashboardService(dashboardService)
    , m_forecastService(forecastService)
{
}

void ApiServer::incomingConnection(qintptr socketDescriptor)
{
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        socket->deleteLater();
        return;
    }

    m_decoders.insert(socket, ev::protocol::FrameDecoder{});

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        readSocket(socket);
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        m_decoders.remove(socket);
        socket->deleteLater();
    });
}

void ApiServer::readSocket(QTcpSocket *socket)
{
    auto decoderIt = m_decoders.find(socket);
    if (decoderIt == m_decoders.end()) {
        return;
    }

    QList<QByteArray> frames;
    try {
        const QByteArray bytes = socket->readAll();
        frames = decoderIt.value().append(QByteArrayView(bytes.constData(), bytes.size()));
    } catch (const ev::protocol::FrameError &error) {
        const auto response = fail(QString(), QStringLiteral("INVALID_REQUEST"), QString::fromLatin1(error.what()));
        const QByteArray payload = ev::protocol::toJson(response);
        socket->write(ev::protocol::encodeFrame(QByteArrayView(payload.constData(), payload.size())));
        socket->disconnectFromHost();
        return;
    }

    for (const QByteArray &frame : frames) {
        ev::protocol::ResponseEnvelope response;
        try {
            response = handleRequest(ev::protocol::parseRequest(frame));
        } catch (const ev::protocol::EnvelopeError &error) {
            response = fail(QString(), error.code(), error.message());
        }
        const QByteArray payload = ev::protocol::toJson(response);
        socket->write(ev::protocol::encodeFrame(QByteArrayView(payload.constData(), payload.size())));
    }
}

ev::protocol::ResponseEnvelope ApiServer::handleRequest(const ev::protocol::RequestEnvelope &request) const
{
    if (request.action == ev::actions::SystemHealth) {
        return ok(request.requestId, QStringLiteral("ready"), m_forecastService->healthState());
    }

    if (request.action == ev::actions::AdminLogin) {
        const Result result = m_authService->login(
            request.payload.value(QStringLiteral("username")).toString(),
            request.payload.value(QStringLiteral("password")).toString());
        if (!result.ok) {
            return fail(request.requestId, result.code, result.message);
        }
        return ok(request.requestId, result.message, QJsonObject{
            {QStringLiteral("username"), request.payload.value(QStringLiteral("username")).toString()}
        });
    }

    if (request.action == ev::actions::AdminDashboard) {
        return ok(request.requestId, QStringLiteral("success"), m_dashboardService->summary());
    }

    if (request.action == ev::actions::ForecastPublish) {
        QJsonObject data;
        const Result result = m_forecastService->publish(request.requestId, request.payload, &data);
        if (!result.ok) {
            return fail(request.requestId, result.code, result.message);
        }
        return ok(request.requestId, result.message, data);
    }

    if (request.action == ev::actions::ForecastLatest) {
        QJsonObject data;
        const Result result = m_forecastService->latest(&data);
        if (!result.ok) {
            return fail(request.requestId, result.code, result.message);
        }
        return ok(request.requestId, result.message, data);
    }

    return fail(request.requestId, QStringLiteral("INVALID_REQUEST"), QStringLiteral("未支持的接口动作"));
}

ev::protocol::ResponseEnvelope ApiServer::ok(const QString &requestId, const QString &message, const QJsonValue &data) const
{
    return {requestId, true, QStringLiteral("OK"), message, data};
}

ev::protocol::ResponseEnvelope ApiServer::fail(const QString &requestId, const QString &code, const QString &message) const
{
    return {requestId, false, code, message, QJsonObject{}};
}


