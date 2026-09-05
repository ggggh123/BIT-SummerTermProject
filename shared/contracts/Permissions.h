#pragma once

#include "contracts/Actions.h"

#include <QString>
#include <QStringView>

namespace ev::permissions {

inline bool isAnonymous(QStringView actorType)
{
    return actorType.isEmpty();
}

inline bool allows(QStringView actorType, QStringView action)
{
    const QString actor = actorType.toString();
    const QString actionText = action.toString();

    if (actionText == ev::actions::SystemHealth) {
        return true;
    }

    if (isAnonymous(actorType)) {
        return actionText == ev::actions::AuthUserLogin
            || actionText == ev::actions::AdminLogin;
    }

    if (actor == QStringLiteral("user")) {
        return actionText == ev::actions::UserGet
            || actionText == ev::actions::UserUpdate
            || actionText == ev::actions::WalletRecharge
            || actionText == ev::actions::StationList
            || actionText == ev::actions::StationDetail
            || actionText == ev::actions::ChargerList
            || actionText == ev::actions::ChargeReserve
            || actionText == ev::actions::ChargeStart
            || actionText == ev::actions::ChargeStop
            || actionText == ev::actions::ChargeSettle
            || actionText == ev::actions::OrderCurrent
            || actionText == ev::actions::OrderList
            || actionText == ev::actions::OrderCancel
            || actionText == ev::actions::ForecastLatest;
    }

    if (actor == QStringLiteral("admin")) {
        return actionText == ev::actions::AdminDashboard
            || actionText == ev::actions::AdminStationCreate
            || actionText == ev::actions::AdminChargerRestart
            || actionText == ev::actions::AdminUserList
            || actionText == ev::actions::AdminUserSetStatus
            || actionText == ev::actions::AdminRequestLogList
            || actionText == ev::actions::StationList
            || actionText == ev::actions::StationDetail
            || actionText == ev::actions::ChargerList
            || actionText == ev::actions::ForecastLatest
            || actionText == ev::actions::DemoReset;
    }

    if (actor == QStringLiteral("simulator")) {
        return actionText == ev::actions::TelemetryPush
            || actionText == ev::actions::SimulatorFaultSet
            || actionText == ev::actions::SimulatorStatus;
    }

    if (actor == QStringLiteral("ml")) {
        return actionText == ev::actions::ForecastPublish;
    }

    return false;
}

} // namespace ev::permissions
