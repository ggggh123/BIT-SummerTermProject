#include "app/AppContext.h"

#include <QDir>
#include <QHostAddress>

Result AppContext::initialize()
{
    Options options;
    return initialize(options);
}

Result AppContext::initialize(const Options &options)
{
    Result result = m_databaseManager.open(options.databasePath);
    if (!result.ok) {
        return result;
    }

    const QString snapshotPath = options.snapshotPath.trimmed().isEmpty()
        ? QDir(QStringLiteral(EV_PROJECT_SOURCE_DIR)).filePath(QStringLiteral("dashboard/runtime/dashboard_snapshot.json"))
        : options.snapshotPath;

    m_authService = std::make_unique<AuthService>(m_databaseManager.database());
    m_dashboardService = std::make_unique<DashboardService>(m_databaseManager.database());
    m_forecastService = std::make_unique<ForecastService>(m_databaseManager.database(), snapshotPath);
    m_apiServer = std::make_unique<ApiServer>(m_authService.get(), m_dashboardService.get(), m_forecastService.get());

    const QString host = options.host.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : options.host.trimmed();
    const QHostAddress address(host);
    if (address.isNull()) {
        return Result::failure(QStringLiteral("NETWORK_ERROR"), QStringLiteral("监听地址无效"));
    }

    m_host = host;
    m_port = options.port == 0 ? 9100 : options.port;
    if (!m_apiServer->listen(address, m_port)) {
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

QString AppContext::host() const
{
    return m_host;
}

quint16 AppContext::port() const
{
    return m_port;
}
