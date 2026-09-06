#pragma once

#include "core/Result.h"
#include "core/BusinessTime.h"
#include "contracts/Actions.h"
#include "contracts/Permissions.h"
#include "contracts/Statuses.h"
#include "protocol/Envelope.h"
#include "services/TokenRoles.h"
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <limits>

// 仅含值，不持有数据库或服务。worker 发布会话副本后，入口可在容量判定前做纯校验。
namespace RequestPreflight {
inline Result authorize(const QString &role, const ev::protocol::RequestEnvelope &request)
{
    if (!ev::actions::all().contains(request.action)) return Result::failure("INVALID_REQUEST","未知接口动作");
    if (request.action==ev::actions::SystemHealth) return Result::success();
    if ((role.isEmpty() && !request.token.trimmed().isEmpty()) || !ev::permissions::allows(role,request.action))
        return Result::failure(role.isEmpty() ? "AUTH_REQUIRED" : "FORBIDDEN","当前身份无权调用此接口");
    return Result::success();
}
inline bool integer(const QJsonValue &value, double minimum=0, double maximum=9007199254740991.0)
{
    const auto number=value.toDouble();
    return value.isDouble() && std::isfinite(number) && std::floor(number)==number && number>=minimum && number<=maximum;
}
inline bool number(const QJsonValue &value, double minimum, double maximum)
{
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble()>=minimum && value.toDouble()<=maximum;
}
inline bool text(const QJsonValue &value) { return value.isString() && !value.toString().trimmed().isEmpty(); }
inline bool timestamp(const QJsonValue &value)
{
    return value.isString() && !BusinessTime::timestampKey(value.toString()).isEmpty();
}
inline Result payload(const QString &action, const QJsonObject &p)
{
    using namespace ev::actions;
    const auto invalid=[] { return Result::failure("INVALID_REQUEST","接口参数类型、范围或结构无效"); };
    const auto id=[&](const char *key) { return integer(p.value(key),1); };
    const auto page=[&] {
        return (!p.contains("limit") || integer(p.value("limit"),1,100))
            && (!p.contains("offset") || integer(p.value("offset")));
    };
    if (action==DemoReset) {
        if (!p.value("confirmation").isString() || p.value("confirmation").toString()!="RESET_DEMO")
            return Result::failure("INVALID_REQUEST","confirmation 必须为 RESET_DEMO");
    } else if (action==AuthUserLogin) {
        if (!p.value("mobile").isString()) return invalid();
        static const QRegularExpression mobile("^1[3-9][0-9]{9}$");
        if (!mobile.match(p.value("mobile").toString().trimmed()).hasMatch()) return Result::failure("INVALID_PHONE","手机号格式无效");
    } else if (action==AdminLogin) {
        if (!text(p.value("username")) || !text(p.value("password"))) return invalid();
    } else if (action==UserUpdate) {
        if (!text(p.value("nickname"))) return invalid();
    } else if (action==WalletRecharge) {
        if (!id("amountFen")) return invalid();
    } else if (action==StationList) {
        if (p.contains("latitude")!=p.contains("longitude") || (p.contains("latitude")
            && (!number(p.value("latitude"),-90,90) || !number(p.value("longitude"),-180,180)))) return invalid();
    } else if (action==StationDetail || action==ChargerList) {
        if (!id("stationId")) return invalid();
    } else if (action==ChargeReserve || action==AdminChargerRestart) {
        if (!id("chargerId")) return invalid();
    } else if (action==ChargeStart || action==ChargeStop || action==ChargeSettle || action==OrderCancel) {
        if (!id("orderId")) return invalid();
    } else if (action==OrderList || action==AdminUserList) {
        if (!page()) return invalid();
        if (action==AdminUserList && p.contains("mobileLike")) {
            static const QRegularExpression filter("^[0-9]{0,11}$");
            if (!p.value("mobileLike").isString() || !filter.match(p.value("mobileLike").toString()).hasMatch()) return invalid();
        }
    } else if (action==AdminDashboard) {
        if (!integer(p.value("rangeDays")) || (p.value("rangeDays").toInt()!=7 && p.value("rangeDays").toInt()!=30)) return invalid();
    } else if (action==AdminStationCreate) {
        if (!text(p.value("name")) || !text(p.value("address")) || !number(p.value("latitude"),-90,90)
            || !number(p.value("longitude"),-180,180) || !id("priceFenPerKwh")
            || !integer(p.value("fastChargerCount")) || !integer(p.value("slowChargerCount"))
            || p.value("fastChargerCount").toDouble()+p.value("slowChargerCount").toDouble()<1) return invalid();
    } else if (action==AdminUserSetStatus) {
        if (!id("userId") || !p.value("status").isString() || !ev::status::isUser(p.value("status").toString())) return invalid();
    } else if (action==TelemetryPush || action==SimulatorFaultSet) {
        if (!id("chargerId") || !timestamp(p.value("recordedAt"))) return invalid();
        if (action==SimulatorFaultSet) { if (!p.value("fault").isBool()) return invalid(); }
        else if (!number(p.value("powerKw"),0,std::numeric_limits<double>::max())
            || !number(p.value("energyIncrementKwh"),0,std::numeric_limits<double>::max())
            || !p.value("status").isString() || !ev::status::isCharger(p.value("status").toString())) return invalid();
    } else if (action==SimulatorStatus) {
        if (!timestamp(p.value("simulatedAt")) || !integer(p.value("eventCount"))
            || (p.value("state").toString()!="running" && p.value("state").toString()!="paused")) return invalid();
    } else if (action==ForecastPublish) {
        // 这里只检查基础结构；依赖站点容量、forecast-enabled 集合及已提交 run 的判定仍归 DB worker。
        if (!text(p.value("runId")) || !text(p.value("modelVersion")) || !p.value("generatedAt").isString()
            || !p.value("dataCutoff").isString() || !p.value("records").isArray()) return invalid();
        for (const auto &value : p.value("records").toArray()) {
            if (!value.isObject()) return invalid();
            const auto record=value.toObject();
            if (!integer(record.value("stationId"),1) || !integer(record.value("horizonH"),-9007199254740991.0)
                || !record.value("forecastAt").isString() || !record.value("predictedLoadKw").isDouble()
                || !integer(record.value("predictedBusyCount"),-9007199254740991.0)
                || !integer(record.value("predictedIdleCount"),-9007199254740991.0)
                || !record.value("congestionLevel").isString() || !record.value("isPeak").isBool()) return invalid();
        }
    }
    return Result::success();
}
inline Result check(const QString &role, const ev::protocol::RequestEnvelope &request)
{
    const auto auth=authorize(role,request);
    return auth.ok ? payload(request.action,request.payload) : auth;
}
} // namespace RequestPreflight
