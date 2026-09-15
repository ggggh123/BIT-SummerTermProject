#pragma once

#include <QStringList>
#include <QStringView>

namespace ev::status {

// 各实体的合法状态取值，与 schema.sql 里的 CHECK 约束一一对应，
// 服务端和协议预检共用这份清单做校验。

inline const QStringList Users = {
    QStringLiteral("active"),
    QStringLiteral("frozen")
};

inline const QStringList Chargers = {
    QStringLiteral("idle"),
    QStringLiteral("reserved"),
    QStringLiteral("charging"),
    QStringLiteral("fault"),
    QStringLiteral("restarting")
};

inline const QStringList Orders = {
    QStringLiteral("reserved"),
    QStringLiteral("charging"),
    QStringLiteral("completed"),
    QStringLiteral("cancelled")
};

inline const QStringList Congestions = {
    QStringLiteral("low"),
    QStringLiteral("medium"),
    QStringLiteral("high")
};

inline const QStringList ForecastRuns = {
    QStringLiteral("active"),
    QStringLiteral("superseded")
};

inline bool contains(const QStringList &values, QStringView value)
{
    return values.contains(value.toString());
}

inline bool isUser(QStringView value)
{
    return contains(Users, value);
}

inline bool isCharger(QStringView value)
{
    return contains(Chargers, value);
}

inline bool isOrder(QStringView value)
{
    return contains(Orders, value);
}

inline bool isCongestion(QStringView value)
{
    return contains(Congestions, value);
}

inline bool isForecastRun(QStringView value)
{
    return contains(ForecastRuns, value);
}

} // namespace ev::status
