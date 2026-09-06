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
        || action == ev::actions::AdminUserSetStatus;
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
                     AdminService *adminService,
                     DashboardService *dashboardService,
                     ForecastService *forecastService,
                     RequestLogService *requestLogService,
                     TelemetryService *telemetryService,
                     UserService *userService,
                     QObject *parent)
    : QTcpServer(parent)
    , m_authService(authService)
    , m_adminService(adminService)
    , m_dashboardService(dashboardService)
    , m_forecastService(forecastService)
    , m_requestLogService(requestLogService)
    , m_telemetryService(telemetryService)
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
        QByteArray payload;
        try {
            const ev::protocol::RequestEnvelope request = ev::protocol::parseRequest(frame);
            payload = dispatch(request);
        } catch (const ev::protocol::EnvelopeError &error) {
            payload = ev::protocol::toJson(fail(QString(), error.code(), error.message()));
        }
        socket->write(ev::protocol::encodeFrame(QByteArrayView(payload.constData(), payload.size())));
    }
}

QByteArray ApiServer::dispatch(const ev::protocol::RequestEnvelope &request) const
{
    QString actor;
    bool mutation = false;
    const QString &action = request.action;
    if (action == ev::actions::UserUpdate || action == ev::actions::WalletRecharge
        || action == ev::actions::ChargeReserve || action == ev::actions::ChargeStart
        || action == ev::actions::ChargeStop || action == ev::actions::ChargeSettle
        || action == ev::actions::OrderCancel) {
        mutation = true;
        if (m_authService->isUserTokenValid(request.token))
            actor = QStringLiteral("user:%1").arg(m_authService->userIdForToken(request.token));
    } else if (action == ev::actions::AdminStationCreate || action == ev::actions::AdminChargerRestart
               || action == ev::actions::AdminUserSetStatus) {
        mutation = true;
        if (m_authService->isTokenValid(request.token))
            actor = QStringLiteral("admin:") + m_authService->adminIdentityForToken(request.token);
    } else if (action == ev::actions::TelemetryPush || action == ev::actions::SimulatorFaultSet
               || action == ev::actions::SimulatorStatus) {
        mutation = true;
        if (m_authService->isSimulatorTokenValid(request.token)) actor = QStringLiteral("simulator");
    } else if (action == ev::actions::ForecastPublish) {
        mutation = true;
        if (m_authService->isMlTokenValid(request.token)) actor = QStringLiteral("ml");
    }
    if (mutation) {
        if (actor.isEmpty()) return ev::protocol::toJson(fail(request.requestId, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("token is missing or invalid")));
        return m_requestLogService->execute(request, actor, [this, &request] { return handleRequest(request); },
                                            action == ev::actions::ForecastPublish);
    }
    const auto response = handleRequest(request);
    const Result logged = m_requestLogService->record(request.requestId, request.action, response);
    if (!logged.ok) return ev::protocol::toJson(fail(request.requestId, logged.code, logged.message));
    return ev::protocol::toJson(response);
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
        return ok(request.requestId, result.message, result.data);
    }

    if (requiresAdminToken(request.action) && !m_authService->isTokenValid(request.token)) {
        return fail(request.requestId, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("admin token is missing or invalid"));
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
        const int rangeDays = request.payload.value(QStringLiteral("rangeDays")).toInt();
        if (rangeDays != 7 && rangeDays != 30) {
            return fail(request.requestId, QStringLiteral("INVALID_REQUEST"), QStringLiteral("rangeDays must be 7 or 30"));
        }
        return ok(request.requestId, QStringLiteral("success"), m_dashboardService->summary(rangeDays));
    }

    QJsonObject adminData;
    Result adminResult;
    if (request.action == ev::actions::AdminStationCreate) {
        adminResult = m_adminService->stationCreate(request.payload, &adminData);
    } else if (request.action == ev::actions::AdminChargerRestart) {
        adminResult = m_adminService->chargerRestart(request.payload, &adminData);
    } else if (request.action == ev::actions::AdminUserList) {
        adminResult = m_adminService->userList(request.payload, &adminData);
    } else if (request.action == ev::actions::AdminUserSetStatus) {
        adminResult = m_adminService->userSetStatus(request.payload, &adminData);
    }
    if (request.action == ev::actions::AdminStationCreate
        || request.action == ev::actions::AdminChargerRestart
        || request.action == ev::actions::AdminUserList
        || request.action == ev::actions::AdminUserSetStatus) {
        if (!adminResult.ok) {
            return fail(request.requestId, adminResult.code, adminResult.message);
        }
        return ok(request.requestId, QStringLiteral("success"), adminData);
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

    if (request.action == ev::actions::ForecastPublish) {
        if (!m_authService->isMlTokenValid(request.token)) {
            return fail(request.requestId, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("ml token is missing or invalid"));
        }
        QJsonObject data;
        const Result result = m_forecastService->publish(request.requestId, request.payload, &data);
        if (!result.ok) {
            return fail(request.requestId, result.code, result.message);
        }
        return ok(request.requestId, result.message, data);
    }

    if (request.action == ev::actions::TelemetryPush
        || request.action == ev::actions::SimulatorFaultSet
        || request.action == ev::actions::SimulatorStatus) {
        if (!m_authService->isSimulatorTokenValid(request.token)) {
            return fail(request.requestId, QStringLiteral("AUTH_REQUIRED"), QStringLiteral("simulator token is missing or invalid"));
        }
        QJsonObject data;
        Result result;
        if (request.action == ev::actions::TelemetryPush) {
            result = m_telemetryService->telemetryPush(request.payload, &data);
        } else if (request.action == ev::actions::SimulatorFaultSet) {
            result = m_telemetryService->faultSet(request.payload, &data);
        } else {
            result = m_telemetryService->simulatorStatus(request.payload, &data);
        }
        if (!result.ok) {
            return fail(request.requestId, result.code, result.message);
        }
        return ok(request.requestId, QStringLiteral("success"), data);
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
    if (code == QStringLiteral("DB_ERROR") || code == QStringLiteral("INTERNAL_ERROR"))
        return {requestId, false, QStringLiteral("INTERNAL_ERROR"), QStringLiteral("数据库操作失败"), QJsonObject{}};
    if (code == QStringLiteral("DB_BUSY"))
        return {requestId, false, code, QStringLiteral("数据库暂时忙，请重试"), QJsonObject{}};
    return {requestId, false, code, message, QJsonObject{}};
}

