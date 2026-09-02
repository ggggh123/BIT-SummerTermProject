#pragma once

#include <QString>

namespace ev::business {

struct ChargeReserveFacts {
    bool userFrozen = false;
    bool activeOrderExists = false;
    bool chargerAvailable = false;
};

inline QString chargeReserveFailure(const ChargeReserveFacts &facts)
{
    if (facts.userFrozen) {
        return QStringLiteral("USER_FROZEN");
    }
    if (facts.activeOrderExists) {
        return QStringLiteral("ACTIVE_ORDER_EXISTS");
    }
    if (!facts.chargerAvailable) {
        return QStringLiteral("CHARGER_NOT_AVAILABLE");
    }
    return {};
}

struct DeviceEventFacts {
    bool chargerExists = false;
    bool temporalConflict = false;
    bool stateTransitionConflict = false;
};

inline QString deviceEventFailure(const DeviceEventFacts &facts)
{
    if (!facts.chargerExists) {
        return QStringLiteral("CHARGER_NOT_AVAILABLE");
    }
    if (facts.temporalConflict || facts.stateTransitionConflict) {
        return QStringLiteral("ORDER_STATE_CONFLICT");
    }
    return {};
}

} // namespace ev::business
