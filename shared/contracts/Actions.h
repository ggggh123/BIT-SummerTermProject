#pragma once

// 三端共用的接口动作（action）清单：客户端/模拟器按这些字符串发请求，
// 服务端 RequestDispatcher 按同一字符串路由到对应 Service 实现。
#include <QString>
#include <QStringList>

namespace ev::actions {

// —— 用户端：账号与钱包 ——
inline const QString AuthUserLogin = QStringLiteral("auth.user_login");       // 用户登录（手机号）
inline const QString UserGet = QStringLiteral("user.get");                    // 查询用户信息/余额
inline const QString UserStatistics = QStringLiteral("user.statistics");      // 用户用电统计
inline const QString UserUpdate = QStringLiteral("user.update");              // 修改用户资料
inline const QString WalletRecharge = QStringLiteral("wallet.recharge");      // 钱包充值

// —— 用户端：站点与充电桩查询 ——
inline const QString StationList = QStringLiteral("station.list");
inline const QString StationDetail = QStringLiteral("station.detail");
inline const QString ChargerList = QStringLiteral("charger.list");

// —— 用户端：充电业务闭环（预约→开始→停止→结算→取消） ——
inline const QString ChargeReserve = QStringLiteral("charge.reserve");
inline const QString ChargeStart = QStringLiteral("charge.start");
inline const QString ChargeStop = QStringLiteral("charge.stop");
inline const QString ChargeSettle = QStringLiteral("charge.settle");
inline const QString OrderCurrent = QStringLiteral("order.current");
inline const QString OrderList = QStringLiteral("order.list");
inline const QString OrderTelemetry = QStringLiteral("order.telemetry");      // 订单功率曲线
inline const QString OrderCancel = QStringLiteral("order.cancel");

// —— 管理端 ——
inline const QString AdminLogin = QStringLiteral("admin.login");
inline const QString AdminDashboard = QStringLiteral("admin.dashboard");
inline const QString AdminStationCreate = QStringLiteral("admin.station_create");
inline const QString AdminChargerRestart = QStringLiteral("admin.charger_restart");
inline const QString AdminUserList = QStringLiteral("admin.user_list");
inline const QString AdminUserSetStatus = QStringLiteral("admin.user_set_status");

// —— 模拟器：遥测与故障上报 ——
inline const QString TelemetryPush = QStringLiteral("telemetry.push");        // 功率/电量采样入库
inline const QString SimulatorFaultSet = QStringLiteral("simulator.fault_set"); // 故障注入/恢复
inline const QString SimulatorStatus = QStringLiteral("simulator.status");     // 同步权威桩快照

// —— ML 预测（optional 模块） ——
inline const QString ForecastPublish = QStringLiteral("forecast.publish");
inline const QString ForecastLatest = QStringLiteral("forecast.latest");

// —— 系统级 ——
inline const QString SystemHealth = QStringLiteral("system.health");
inline const QString DemoReset = QStringLiteral("demo.reset");

// 全量清单：服务端用它校验 action 是否合法
inline QStringList all()
{
    return {
        AuthUserLogin,
        UserGet,
        UserStatistics,
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
        OrderTelemetry,
        OrderCancel,
        AdminLogin,
        AdminDashboard,
        AdminStationCreate,
        AdminChargerRestart,
        AdminUserList,
        AdminUserSetStatus,
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
