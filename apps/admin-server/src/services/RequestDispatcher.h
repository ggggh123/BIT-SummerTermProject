#pragma once
#include "protocol/Envelope.h"
#include "services/AdminService.h"
#include "services/AuthService.h"
#include "services/DashboardService.h"
#include "services/ForecastService.h"
#include "services/RequestLogService.h"
#include "services/TelemetryService.h"
#include "services/UserService.h"
#include "services/DemoResetService.h"

// 仅由 DatabaseWorker 在数据库所属线程创建和调用。
class RequestDispatcher {
public:
    RequestDispatcher(AuthService *auth, AdminService *admin, DashboardService *dashboard,
                      ForecastService *forecast, RequestLogService *log, TelemetryService *telemetry, UserService *user, DemoResetService *reset)
        : m_authService(auth), m_adminService(admin), m_dashboardService(dashboard),
          m_forecastService(forecast), m_requestLogService(log), m_telemetryService(telemetry), m_userService(user), m_resetService(reset) {}
    QByteArray dispatch(const ev::protocol::RequestEnvelope &request) const;
private:
    ev::protocol::ResponseEnvelope handleRequest(const ev::protocol::RequestEnvelope &request) const;
    ev::protocol::ResponseEnvelope ok(const QString &, const QString &, const QJsonValue &data = QJsonObject{}) const;
    ev::protocol::ResponseEnvelope fail(const QString &, const QString &, const QString &) const;
    AuthService *m_authService;
    AdminService *m_adminService;
    DashboardService *m_dashboardService;
    ForecastService *m_forecastService;
    RequestLogService *m_requestLogService;
    TelemetryService *m_telemetryService;
    UserService *m_userService;
    DemoResetService *m_resetService;
};
