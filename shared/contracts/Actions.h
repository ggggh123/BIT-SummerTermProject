#pragma once

#include <QString>
#include <QStringList>

namespace ev::actions {

inline const QString AuthUserLogin = QStringLiteral("auth.user_login");
inline const QString UserGet = QStringLiteral("user.get");
inline const QString UserUpdate = QStringLiteral("user.update");
inline const QString WalletRecharge = QStringLiteral("wallet.recharge");

inline const QString StationList = QStringLiteral("station.list");
inline const QString StationDetail = QStringLiteral("station.detail");
inline const QString ChargerList = QStringLiteral("charger.list");

inline const QString ChargeReserve = QStringLiteral("charge.reserve");
inline const QString ChargeStart = QStringLiteral("charge.start");
inline const QString ChargeStop = QStringLiteral("charge.stop");
inline const QString ChargeSettle = QStringLiteral("charge.settle");
inline const QString OrderCurrent = QStringLiteral("order.current");
inline const QString OrderList = QStringLiteral("order.list");
inline const QString OrderCancel = QStringLiteral("order.cancel");

inline const QString AdminLogin = QStringLiteral("admin.login");
inline const QString AdminDashboard = QStringLiteral("admin.dashboard");
inline const QString AdminStationCreate = QStringLiteral("admin.station_create");
inline const QString AdminChargerRestart = QStringLiteral("admin.charger_restart");
inline const QString AdminUserList = QStringLiteral("admin.user_list");
inline const QString AdminUserSetStatus = QStringLiteral("admin.user_set_status");
inline const QString AdminRequestLogList = QStringLiteral("admin.request_log_list");

inline const QString TelemetryPush = QStringLiteral("telemetry.push");
inline const QString SimulatorFaultSet = QStringLiteral("simulator.fault_set");
inline const QString SimulatorStatus = QStringLiteral("simulator.status");

inline const QString ForecastPublish = QStringLiteral("forecast.publish");
inline const QString ForecastLatest = QStringLiteral("forecast.latest");

inline const QString SystemHealth = QStringLiteral("system.health");
inline const QString DemoReset = QStringLiteral("demo.reset");

inline QStringList all()
{
    return {
        AuthUserLogin,
        UserGet,
        UserUpdate,
        WalletRecharge,
        StationList,
        StationDetail,
        ChargerList,
        ChargeReserve,
        ChargeStart,
        ChargeStop,
        ChargeSettle,
        OrderCurrent,
        OrderList,
        OrderCancel,
        AdminLogin,
        AdminDashboard,
        AdminStationCreate,
        AdminChargerRestart,
        AdminUserList,
        AdminUserSetStatus,
        AdminRequestLogList,
        TelemetryPush,
        SimulatorFaultSet,
        SimulatorStatus,
        ForecastPublish,
        ForecastLatest,
        SystemHealth,
        DemoReset
    };
}

} // namespace ev::actions
