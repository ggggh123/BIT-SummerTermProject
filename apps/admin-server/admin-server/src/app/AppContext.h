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
    Result initialize();

    AuthService *authService() const;
    DashboardService *dashboardService() const;
    ApiServer *apiServer() const;
    QString databasePath() const;

private:
    DatabaseManager m_databaseManager;
    std::unique_ptr<AuthService> m_authService;
    std::unique_ptr<DashboardService> m_dashboardService;
    std::unique_ptr<ForecastService> m_forecastService;
    std::unique_ptr<ApiServer> m_apiServer;
};
