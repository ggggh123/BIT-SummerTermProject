#include "network/ApiServer.h"

#include "contracts/Actions.h"
#include "protocol/JsonEnvelope.h"

#include <QJsonObject>
#include <QTcpSocket>

namespace {

bool requiresAdminToken(const QString &action)
{
    return action == ev::actions::AdminDashboard
        || action == ev::actions::AdminStationCreate
        || action == ev::actions::AdminChargerRestart
        || action == ev::actions::AdminUserList
        || action == ev::actions::AdminUserSetStatus
        || action == ev::actions::AdminRequestLogList;
}

bool allowsUserOrAdminToken(const QString &action)
{
    return action == ev::actions::StationList
        || action == ev::actions::StationDetail
        || action == ev::actions::ChargerList
        || action == ev::actions::ForecastLatest;
}

bool requiresUserToken(const QString &action)
{
    return action == ev::actions::UserGet
        || action == ev::actions::UserUpdate
        || action == ev::actions::WalletRecharge
        || action == ev::actions::ChargeReserve
        || action == ev::actions::ChargeStart
        || action == ev::actions::ChargeStop
        || action == ev::actions::ChargeSettle
        || action == ev::actions::OrderCurrent
        || action == ev::actions::OrderList
        || action == ev::actions::OrderCancel;
}

} // namespace

ApiServer::ApiServer(AuthService *authService,
                     DashboardService *dashboardService,
                     ForecastService *forecastService,
                     RequestLogService *requestLogService,
                     UserService *userService,
                     QObject *parent)
    : QTcpServer(parent)
    , m_authService(authService)
    , m_dashboardService(dashboardService)
    , m_forecastService(forecastService)
    , m_requestLogService(requestLogService)
    , m_userService(userService)
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
            const ev::protocol::RequestEnvelope request = ev::protocol::parseRequest(frame);
            response = handleRequest(request);
            m_requestLogService->record(request.requestId, request.action, response);
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

    if (request.action == ev::actions::AuthUserLogin) {
        const LoginResult result = m_authService->loginUser(
            request.payload.value(QStringLiteral("mobile")).toString());
        if (!result.ok) {
            return fail(request.requestId, result.code, result.message);
        }
        return ok(request.requestId, result.message, result.data);
    }

    if (request.action == ev::actions::AdminLogin) {
        const LoginResult result = m_authService->login(
            request.payload.value(QStringLiteral("username")).toString(),
            request.payload.value(QStringLiteral("password")).toString());
        if (!result.ok) {
            return fail(request.requestId, result.code, result.message);
        }
        return ok(request.requestId, result.message, QJsonObject{
            {QStringLiteral("username"), request.payload.value(QStringLiteral("username")).toString()},
            {QStringLiteral("token"), result.token}
        });
    }

    if (requiresAdminToken(request.action) && !m_authService->isTokenValid(request.token)) {
        return fail(request.requestId, QStringLiteral("UNAUTHORIZED"), QStringLiteral("admin token is missing or invalid"));
    }
    if (requiresUserToken(request.action) && !m_authService->isUserTokenValid(request.token)) {
        return fail(request.requestId, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("user token is missing or invalid"));
    }
    if (allowsUserOrAdminToken(request.action)
        && !m_authService->isUserTokenValid(request.token)
        && !m_authService->isTokenValid(request.token)) {
        return fail(request.requestId, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("token is missing or invalid"));
    }

    if (request.action == ev::actions::AdminDashboard) {
        return ok(request.requestId, QStringLiteral("success"), m_dashboardService->summary());
    }

    const int userId = m_authService->userIdForToken(request.token);
    QJsonObject userData;
    Result userResult;
    if (request.action == ev::actions::UserGet) {
        userResult = m_userService->getUser(userId, &userData);
    } else if (request.action == ev::actions::UserUpdate) {
        userResult = m_userService->updateUser(userId, request.payload, &userData);
    } else if (request.action == ev::actions::WalletRecharge) {
        userResult = m_userService->recharge(userId, request.payload, &userData);
    } else if (request.action == ev::actions::StationList) {
        userResult = m_userService->stationList(request.payload, &userData);
    } else if (request.action == ev::actions::StationDetail) {
        userResult = m_userService->stationDetail(request.payload, &userData);
    } else if (request.action == ev::actions::ChargerList) {
        userResult = m_userService->chargerList(request.payload, &userData);
    } else if (request.action == ev::actions::OrderCurrent) {
        userResult = m_userService->currentOrder(userId, &userData);
    } else if (request.action == ev::actions::OrderList) {
        userResult = m_userService->orderList(userId, request.payload, &userData);
    } else if (request.action == ev::actions::ChargeReserve) {
        userResult = m_userService->reserve(userId, request.payload, &userData);
    } else if (request.action == ev::actions::ChargeStart) {
        userResult = m_userService->start(userId, request.payload, &userData);
    } else if (request.action == ev::actions::ChargeStop) {
        userResult = m_userService->stop(userId, request.payload, &userData);
    } else if (request.action == ev::actions::ChargeSettle) {
        userResult = m_userService->settle(userId, request.payload, &userData);
    } else if (request.action == ev::actions::OrderCancel) {
        userResult = m_userService->cancel(userId, request.payload, &userData);
    }
    if (request.action == ev::actions::UserGet || request.action == ev::actions::UserUpdate
        || request.action == ev::actions::WalletRecharge || request.action == ev::actions::StationList
        || request.action == ev::actions::StationDetail || request.action == ev::actions::ChargerList
        || request.action == ev::actions::OrderCurrent || request.action == ev::actions::OrderList
        || request.action == ev::actions::ChargeReserve || request.action == ev::actions::ChargeStart
        || request.action == ev::actions::ChargeStop || request.action == ev::actions::ChargeSettle
        || request.action == ev::actions::OrderCancel) {
        if (!userResult.ok) {
            return fail(request.requestId, userResult.code, userResult.message);
        }
        return ok(request.requestId, QStringLiteral("success"), userData);
    }

    if (request.action == ev::actions::AdminRequestLogList) {
        QJsonObject data;
        const Result result = m_requestLogService->list(
            request.payload.value(QStringLiteral("requestId")).toString(),
            request.payload.value(QStringLiteral("limit")).toInt(20),
            request.payload.value(QStringLiteral("offset")).toInt(0),
            &data);
        if (!result.ok) {
            return fail(request.requestId, result.code, result.message);
        }
        return ok(request.requestId, QStringLiteral("success"), data);
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


