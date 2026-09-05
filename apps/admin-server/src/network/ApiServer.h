#pragma once

#include "protocol/Envelope.h"
#include "protocol/FrameCodec.h"
#include "services/AdminService.h"
#include "services/AuthService.h"
#include "services/DashboardService.h"
#include "services/ForecastService.h"
#include "services/RequestLogService.h"
#include "services/TelemetryService.h"
#include "services/UserService.h"

#include <QHash>
#include <QJsonValue>
#include <QTcpServer>

class QTcpSocket;

class ApiServer : public QTcpServer
{
    Q_OBJECT

public:
    ApiServer(AuthService *authService,
              AdminService *adminService,
              DashboardService *dashboardService,
              ForecastService *forecastService,
              RequestLogService *requestLogService,
              TelemetryService *telemetryService,
              UserService *userService,
              QObject *parent = nullptr);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    void readSocket(QTcpSocket *socket);
    QByteArray dispatch(const ev::protocol::RequestEnvelope &request) const;
    ev::protocol::ResponseEnvelope handleRequest(const ev::protocol::RequestEnvelope &request) const;
    ev::protocol::ResponseEnvelope ok(const QString &requestId, const QString &message, const QJsonValue &data = {}) const;
    ev::protocol::ResponseEnvelope fail(const QString &requestId, const QString &code, const QString &message) const;

    AuthService *m_authService = nullptr;
    AdminService *m_adminService = nullptr;
    DashboardService *m_dashboardService = nullptr;
    ForecastService *m_forecastService = nullptr;
    RequestLogService *m_requestLogService = nullptr;
    TelemetryService *m_telemetryService = nullptr;
    UserService *m_userService = nullptr;
    QHash<QTcpSocket *, ev::protocol::FrameDecoder> m_decoders;
};
