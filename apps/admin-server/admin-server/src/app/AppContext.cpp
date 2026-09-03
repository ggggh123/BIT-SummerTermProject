#include "app/AppContext.h"

#include <QDir>
#include <QHostAddress>

Result AppContext::initialize()
{
    Result result = m_databaseManager.open();
    if (!result.ok) {
        return result;
    }

    const QString snapshotPath = QDir(QStringLiteral(EV_PROJECT_SOURCE_DIR))
                                     .filePath(QStringLiteral("dashboard/runtime/dashboard_snapshot.json"));

    m_authService = std::make_unique<AuthService>(m_databaseManager.database());
    m_dashboardService = std::make_unique<DashboardService>(m_databaseManager.database());
    m_forecastService = std::make_unique<ForecastService>(m_databaseManager.database(), snapshotPath);
    m_apiServer = std::make_unique<ApiServer>(m_authService.get(), m_dashboardService.get(), m_forecastService.get());

    if (!m_apiServer->listen(QHostAddress::LocalHost, 4545)) {
        return Result::failure(QStringLiteral("NETWORK_ERROR"), m_apiServer->errorString());
    }

    return Result::success();
}

AuthService *AppContext::authService() const
{
    return m_authService.get();
}

DashboardService *AppContext::dashboardService() const
{
    return m_dashboardService.get();
}

ApiServer *AppContext::apiServer() const
{
    return m_apiServer.get();
}

QString AppContext::databasePath() const
{
    return m_databaseManager.databasePath();
}
