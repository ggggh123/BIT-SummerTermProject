#pragma once

#include "db/DatabaseManager.h"
#include "network/ApiServer.h"
#include "services/AuthService.h"
#include "services/DashboardService.h"
#include "services/ForecastService.h"

#include <memory>

class AppContext
{
public:
    struct Options
    {
        QString databasePath;
        QString host = QStringLiteral("127.0.0.1");
        quint16 port = 9100;
        QString snapshotPath;
    };

    Result initialize();
    Result initialize(const Options &options);

    AuthService *authService() const;
    DashboardService *dashboardService() const;
    ApiServer *apiServer() const;
    QString databasePath() const;
    QString host() const;
    quint16 port() const;

private:
    DatabaseManager m_databaseManager;
    std::unique_ptr<AuthService> m_authService;
    std::unique_ptr<DashboardService> m_dashboardService;
    std::unique_ptr<ForecastService> m_forecastService;
    std::unique_ptr<ApiServer> m_apiServer;
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 9100;
};
