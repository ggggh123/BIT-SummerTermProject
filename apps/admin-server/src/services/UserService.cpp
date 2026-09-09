#include "services/UserService.h"
#include "core/BusinessTime.h"
#include "db/SqlTransaction.h"
#include "services/EnergyReadModel.h"

#include <QDateTime>
#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>
#include <QtMath>
#include <cmath>

namespace {

QString nowIso()
{
    return BusinessTime::now();
}

QJsonValue nullableText(const QString &value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

bool isInteger(const QJsonValue &value, int *out)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number <= 0.0) {
        return false;
    }
    *out = value.toInt();
    return *out > 0;
}

bool pageArgs(const QJsonObject &payload, int *limit, int *offset)
{
    *limit = payload.value(QStringLiteral("limit")).toInt(20);
    *offset = payload.value(QStringLiteral("offset")).toInt(0);
    return *limit >= 1 && *limit <= 100 && *offset >= 0;
}

double distanceKm(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double earthKm = 6371.0;
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1);
    const double a = qSin(dLat / 2) * qSin(dLat / 2)
        + qCos(qDegreesToRadians(lat1)) * qCos(qDegreesToRadians(lat2))
        * qSin(dLon / 2) * qSin(dLon / 2);
    return 2 * earthKm * qAtan2(qSqrt(a), qSqrt(1 - a));
}

} // namespace

UserService::UserService(QSqlDatabase database)
    : m_database(database)
{
}

Result UserService::orderTelemetry(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    int id=0;
    if (userId<=0 || !isInteger(payload.value("orderId"),&id))
        return Result::failure("INVALID_REQUEST",QStringLiteral("订单参数无效"));
    return EnergyReadModel::order(m_database,id,userId,responseData);
}

Result UserService::getUser(int userId, QJsonObject *responseData) const
{
    if (responseData) {
        responseData->insert(QStringLiteral("user"), userObject(userId));
    }
    return Result::success();
}

Result UserService::usageStatistics(int userId, QJsonObject *responseData) const
{
    if (userId <= 0) return Result::failure("AUTH_REQUIRED", QStringLiteral("请先登录"));
    const QString asOf = BusinessTime::now();
    const QDate today = QDateTime::fromString(asOf, Qt::ISODate).date();
    qint64 orderCount = 0, completedCount = 0, paidFen = 0, durationSec = 0;
    qint64 pendingCount = 0, pendingFen = 0;
    double energyKwh = 0, monthEnergyKwh = 0;
    double daily[7]{};

    // 一个 SELECT 的一致性快照；身份来自已认证 token，不接受调用者指定 userId。
    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    query.prepare(QStringLiteral(
        "SELECT status, started_at, ended_at, energy_kwh, amount_fen FROM orders "
        "WHERE user_id = ? AND status IN ('charging', 'completed') AND started_at IS NOT NULL"));
    query.addBindValue(userId);
    if (!query.exec()) return Result::failure("INTERNAL_ERROR", QStringLiteral("个人用电统计读取失败"));
    while (query.next()) {
        const QString status = query.value(0).toString();
        const QString started = query.value(1).toString();
        const QString ended = query.value(2).toString();
        const double energy = query.value(3).toDouble();
        const qint64 amount = query.value(4).toLongLong();
        const QDate date = QDateTime::fromString(started, Qt::ISODate).toOffsetFromUtc(8 * 3600).date();
        ++orderCount;
        energyKwh += energy;
        durationSec += BusinessTime::elapsed(started, ended.isEmpty() ? asOf : ended);
        if (status == QStringLiteral("completed")) {
            ++completedCount;
            paidFen += amount;
        } else if (!ended.isEmpty()) {
            ++pendingCount;
            pendingFen += amount;
        }
        if (date.year() == today.year() && date.month() == today.month()) monthEnergyKwh += energy;
        const qint64 daysAgo = date.daysTo(today);
        if (daysAgo >= 0 && daysAgo < 7) daily[6 - daysAgo] += energy;
    }
    if (query.lastError().isValid()) return Result::failure("INTERNAL_ERROR", QStringLiteral("个人用电统计读取失败"));
    QJsonArray days;
    for (int i = 0; i < 7; ++i) days.append(QJsonObject{
        {"date", today.addDays(i - 6).toString(Qt::ISODate)}, {"energyKwh", daily[i]}});
    if (responseData) *responseData = QJsonObject{
        {"userId", userId}, {"asOf", asOf}, {"orderCount", orderCount},
        {"completedCount", completedCount}, {"energyKwh", energyKwh}, {"paidFen", paidFen},
        {"durationSec", durationSec}, {"pendingSettlementCount", pendingCount},
        {"pendingSettlementFen", pendingFen}, {"monthEnergyKwh", monthEnergyKwh}, {"days", days}};
    return Result::success();
}

Result UserService::updateUser(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    const QString nickname = payload.value(QStringLiteral("nickname")).toString().trimmed();
    if (nickname.isEmpty()) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("nickname is required"));
    }
    SqlTransaction database(m_database);
    if (!database.transaction()) return databaseFailure(database.lastError());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE users SET nickname=? WHERE id=?"));
    query.addBindValue(nickname);
    query.addBindValue(userId);
    if (!query.exec()) {
        return databaseFailure(query.lastError());
    }
    if (!database.commit()) return databaseFailure(database.lastError());
    return getUser(userId, responseData);
}

Result UserService::recharge(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    int amountFen = 0;
    if (!isInteger(payload.value(QStringLiteral("amountFen")), &amountFen)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("amountFen is invalid"));
    }
    SqlTransaction database(m_database);
    if (!database.transaction()) return databaseFailure(database.lastError());
    if (userFrozen(userId)) {
        return Result::failure(QStringLiteral("USER_FROZEN"), QStringLiteral("user is frozen"));
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE users SET balance_fen=balance_fen+? WHERE id=?"));
    query.addBindValue(amountFen);
    query.addBindValue(userId);
    if (!query.exec()) {
        return databaseFailure(query.lastError());
    }
    query.prepare(QStringLiteral("SELECT balance_fen FROM users WHERE id=?"));
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        return databaseFailure(query.lastError());
    }
    if (responseData) {
        responseData->insert(QStringLiteral("userId"), userId);
        responseData->insert(QStringLiteral("balanceFen"), query.value(0).toLongLong());
    }
    query.finish();
    if (!database.commit()) return databaseFailure(database.lastError());
    return Result::success();
}

Result UserService::stationList(const QJsonObject &payload, QJsonObject *responseData) const
{
    const bool hasLatitude = payload.contains(QStringLiteral("latitude"));
    const bool hasLongitude = payload.contains(QStringLiteral("longitude"));
    if (hasLatitude != hasLongitude) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("latitude and longitude must appear together"));
    }
    const double latitude = payload.value(QStringLiteral("latitude")).toDouble();
    const double longitude = payload.value(QStringLiteral("longitude")).toDouble();
    if (hasLatitude && (!std::isfinite(latitude) || !std::isfinite(longitude)
            || latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("coordinates are invalid"));
    }

    QJsonArray stations;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT id FROM stations ORDER BY id"))) {
        return databaseFailure(query.lastError());
    }
    while (query.next()) {
        stations.append(stationObject(query.value(0).toInt(), hasLatitude, latitude, longitude));
    }
    if (responseData) {
        responseData->insert(QStringLiteral("stations"), stations);
    }
    return Result::success();
}

Result UserService::stationDetail(const QJsonObject &payload, QJsonObject *responseData) const
{
    int stationId = 0;
    const Result idResult = requirePositiveId(payload, QStringLiteral("stationId"), &stationId);
    if (!idResult.ok) {
        return idResult;
    }
    QJsonObject station = stationObject(stationId, false);
    if (station.isEmpty()) {
        return Result::failure(QStringLiteral("ENTITY_NOT_FOUND"), QStringLiteral("station not found"));
    }
    QJsonArray chargers;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM chargers WHERE station_id=? ORDER BY CAST(code AS INTEGER)"));
    query.addBindValue(stationId);
    if (!query.exec()) {
        return databaseFailure(query.lastError());
    }
    while (query.next()) {
        chargers.append(chargerObject(query.value(0).toInt()));
    }
    if (responseData) {
        responseData->insert(QStringLiteral("station"), station);
        responseData->insert(QStringLiteral("chargers"), chargers);
    }
    return Result::success();
}

Result UserService::chargerList(const QJsonObject &payload, QJsonObject *responseData) const
{
    QJsonObject detail;
    const Result result = stationDetail(payload, &detail);
    if (!result.ok) {
        return result;
    }
    if (responseData) {
        responseData->insert(QStringLiteral("chargers"), detail.value(QStringLiteral("chargers")).toArray());
    }
    return Result::success();
}

Result UserService::currentOrder(int userId, QJsonObject *responseData) const
{
    const int orderId = activeOrderId(userId);
    if (responseData) {
        responseData->insert(QStringLiteral("order"), orderId > 0 ? QJsonValue(orderObject(orderId)) : QJsonValue(QJsonValue::Null));
    }
    return Result::success();
}

Result UserService::orderList(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    int limit = 20;
    int offset = 0;
    if (!pageArgs(payload, &limit, &offset)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("pagination is invalid"));
    }
    QSqlQuery countQuery(m_database);
    countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE user_id=?"));
    countQuery.addBindValue(userId);
    if (!countQuery.exec() || !countQuery.next()) {
        return databaseFailure(countQuery.lastError());
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM orders WHERE user_id=? ORDER BY reserved_at DESC LIMIT ? OFFSET ?"));
    query.addBindValue(userId);
    query.addBindValue(limit);
    query.addBindValue(offset);
    if (!query.exec()) {
        return databaseFailure(query.lastError());
    }
    QJsonArray items;
    while (query.next()) {
        items.append(orderObject(query.value(0).toInt()));
    }
    if (responseData) {
        responseData->insert(QStringLiteral("items"), items);
        responseData->insert(QStringLiteral("total"), countQuery.value(0).toInt());
    }
    return Result::success();
}

Result UserService::reserve(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    int chargerId = 0;
    Result idResult = requirePositiveId(payload, QStringLiteral("chargerId"), &chargerId);
    if (!idResult.ok) {
        return idResult;
    }
    SqlTransaction database(m_database);
    if (!database.transaction()) return databaseFailure(database.lastError());
    if (userFrozen(userId)) {
        return Result::failure(QStringLiteral("USER_FROZEN"), QStringLiteral("user is frozen"));
    }
    if (activeOrderId(userId) > 0) {
        return Result::failure(QStringLiteral("ACTIVE_ORDER_EXISTS"), QStringLiteral("active order exists"));
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT status FROM chargers WHERE id=? AND NOT EXISTS (SELECT 1 FROM orders WHERE charger_id=chargers.id AND status IN ('reserved','charging'))"));
    query.addBindValue(chargerId);
    if (!query.exec()) return databaseFailure(query.lastError());
    if (!query.next() || query.value(0).toString() != QStringLiteral("idle")) {
        return Result::failure(QStringLiteral("CHARGER_NOT_AVAILABLE"), QStringLiteral("charger is not available"));
    }
    query.prepare(QStringLiteral("UPDATE chargers SET status='reserved', updated_at=? WHERE id=? AND status='idle'"));
    query.addBindValue(nowIso());
    query.addBindValue(chargerId);
    if (!query.exec()) return databaseFailure(query.lastError());
    if (query.numRowsAffected() != 1) {
        database.rollback();
        return Result::failure(QStringLiteral("CHARGER_NOT_AVAILABLE"), QStringLiteral("charger is not available"));
    }
    query.prepare(QStringLiteral("INSERT INTO orders(user_id, charger_id, status, reserved_at, energy_kwh, amount_fen) VALUES(?, ?, 'reserved', ?, 0, 0)"));
    query.addBindValue(userId);
    query.addBindValue(chargerId);
    query.addBindValue(nowIso());
    if (!query.exec()) {
        database.rollback();
        return databaseFailure(query.lastError());
    }
    const int orderId = query.lastInsertId().toInt();
    if (!database.commit()) return databaseFailure(database.lastError());
    if (responseData) {
        responseData->insert(QStringLiteral("order"), orderObject(orderId));
    }
    return Result::success();
}

// charge.start：开始充电。写操作标准模板——参数校验→开事务→前置检查→SELECT 查现状→UPDATE 改数据→commit。
Result UserService::start(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    int orderId = 0;
    Result idResult = requirePositiveId(payload, QStringLiteral("orderId"), &orderId);   // 1) 参数校验
    if (!idResult.ok) {
        return idResult;
    }
    SqlTransaction database(m_database);    // 2) 开事务（嵌套时自动降级 SAVEPOINT）
    if (!database.transaction()) return databaseFailure(database.lastError());
    if (orderOwner(orderId) == 0) {         // 3) 前置检查：订单存在/归属/用户未冻结
        return Result::failure(QStringLiteral("ENTITY_NOT_FOUND"), QStringLiteral("order not found"));
    }
    if (orderOwner(orderId) != userId) {
        return Result::failure(QStringLiteral("FORBIDDEN"), QStringLiteral("order belongs to another user"));
    }
    if (userFrozen(userId)) {
        return Result::failure(QStringLiteral("USER_FROZEN"), QStringLiteral("user is frozen"));
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT o.charger_id, o.status, c.status FROM orders o JOIN chargers c ON c.id=o.charger_id WHERE o.id=?"));  // 4) SELECT 查现状
    query.addBindValue(orderId);   // ? 占位符绑定参数，防 SQL 注入
    if (!query.exec()) return databaseFailure(query.lastError());
    if (!query.next() || query.value(1).toString() != QStringLiteral("reserved") || query.value(2).toString() != QStringLiteral("reserved")) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("order is not reserved"));  // 订单和桩都必须是"已预约"
    }
    const int chargerId = query.value(0).toInt();
    query.prepare(QStringLiteral("UPDATE orders SET status='charging', started_at=? WHERE id=?"));   // 5) 改订单状态
    query.addBindValue(nowIso());
    query.addBindValue(orderId);
    if (!query.exec()) {
        return databaseFailure(query.lastError());
    }
    query.prepare(QStringLiteral("UPDATE chargers SET status='charging', updated_at=? WHERE id=?")); // 5) 同步改桩状态
    query.addBindValue(nowIso());
    query.addBindValue(chargerId);
    if (!query.exec()) return databaseFailure(query.lastError());
    if (!database.commit()) return databaseFailure(database.lastError());   // 6) 提交；中途任何失败由析构自动回滚
    if (responseData) {
        responseData->insert(QStringLiteral("order"), orderObject(orderId));
    }
    return Result::success();
}

Result UserService::stop(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    int orderId = 0;
    Result idResult = requirePositiveId(payload, QStringLiteral("orderId"), &orderId);
    if (!idResult.ok) {
        return idResult;
    }
    SqlTransaction database(m_database);
    if (!database.transaction()) return databaseFailure(database.lastError());
    if (orderOwner(orderId) == 0) {
        return Result::failure(QStringLiteral("ENTITY_NOT_FOUND"), QStringLiteral("order not found"));
    }
    if (orderOwner(orderId) != userId) {
        return Result::failure(QStringLiteral("FORBIDDEN"), QStringLiteral("order belongs to another user"));
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT status, ended_at, started_at, charger_id FROM orders WHERE id=?"));
    query.addBindValue(orderId);
    if (!query.exec()) return databaseFailure(query.lastError());
    if (!query.next() || query.value(0).toString() != QStringLiteral("charging") || !query.value(1).toString().isEmpty()) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("order is not active charging"));
    }
    query.prepare(QStringLiteral("UPDATE orders SET ended_at=? WHERE id=?"));
    query.addBindValue(nowIso());
    query.addBindValue(orderId);
    if (!query.exec()) {
        return databaseFailure(query.lastError());
    }
    if (!database.commit()) return databaseFailure(database.lastError());
    if (responseData) {
        responseData->insert(QStringLiteral("order"), orderObject(orderId));
    }
    return Result::success();
}

// charge.settle：结算扣费。订单置 completed、从余额扣款（整数分）、桩计数+累计时长，三步在同一事务里原子完成。
Result UserService::settle(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    int orderId = 0;
    Result idResult = requirePositiveId(payload, QStringLiteral("orderId"), &orderId);
    if (!idResult.ok) {
        return idResult;
    }
    SqlTransaction database(m_database);
    if (!database.transaction()) return databaseFailure(database.lastError());
    if (orderOwner(orderId) == 0) {
        return Result::failure(QStringLiteral("ENTITY_NOT_FOUND"), QStringLiteral("order not found"));
    }
    if (orderOwner(orderId) != userId) {
        return Result::failure(QStringLiteral("FORBIDDEN"), QStringLiteral("order belongs to another user"));
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT status, ended_at, amount_fen, charger_id, started_at FROM orders WHERE id=?"));
    query.addBindValue(orderId);
    if (!query.exec()) return databaseFailure(query.lastError());
    if (!query.next() || query.value(0).toString() != QStringLiteral("charging") || query.value(1).toString().isEmpty()) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("order is not stopped"));
    }
    const qint64 amountFen = query.value(2).toLongLong();
    const int chargerId = query.value(3).toInt();
    const qint64 duration = BusinessTime::elapsed(query.value(4).toString(), query.value(1).toString());
    query.prepare(QStringLiteral("SELECT balance_fen FROM users WHERE id=?"));   // 余额校验（整数分，避免浮点误差）
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) return databaseFailure(query.lastError());
    if (query.value(0).toLongLong() < amountFen) {
        return Result::failure(QStringLiteral("INSUFFICIENT_BALANCE"), QStringLiteral("balance is insufficient"));
    }
    query.prepare(QStringLiteral("UPDATE users SET balance_fen=balance_fen-? WHERE id=?"));   // 钱包余额扣款
    query.addBindValue(amountFen);
    query.addBindValue(userId);
    if (!query.exec()) return databaseFailure(query.lastError());
    query.prepare(QStringLiteral("UPDATE orders SET status='completed' WHERE id=?"));   // 订单完结
    query.addBindValue(orderId);
    if (!query.exec()) return databaseFailure(query.lastError());
    query.prepare(QStringLiteral("UPDATE chargers SET status=CASE WHEN status='charging' THEN 'idle' ELSE status END, charge_count=charge_count+1, total_duration_sec=total_duration_sec+?, updated_at=? WHERE id=?"));   // 桩：空闲+计数+累计时长
    query.addBindValue(duration);
    query.addBindValue(nowIso());
    query.addBindValue(chargerId);
    if (!query.exec()) return databaseFailure(query.lastError());
    query.prepare(QStringLiteral("SELECT balance_fen FROM users WHERE id=?"));
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) return databaseFailure(query.lastError());
    const qint64 balance = query.value(0).toLongLong();
    query.finish();
    if (!database.commit()) return databaseFailure(database.lastError());
    if (responseData) {
        responseData->insert(QStringLiteral("order"), orderObject(orderId));
        responseData->insert(QStringLiteral("balanceFen"), balance);
    }
    return Result::success();
}

Result UserService::cancel(int userId, const QJsonObject &payload, QJsonObject *responseData) const
{
    int orderId = 0;
    Result idResult = requirePositiveId(payload, QStringLiteral("orderId"), &orderId);
    if (!idResult.ok) {
        return idResult;
    }
    SqlTransaction database(m_database);
    if (!database.transaction()) return databaseFailure(database.lastError());
    if (orderOwner(orderId) == 0) {
        return Result::failure(QStringLiteral("ENTITY_NOT_FOUND"), QStringLiteral("order not found"));
    }
    if (orderOwner(orderId) != userId) {
        return Result::failure(QStringLiteral("FORBIDDEN"), QStringLiteral("order belongs to another user"));
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT o.charger_id, o.status, c.status FROM orders o JOIN chargers c ON c.id=o.charger_id WHERE o.id=?"));
    query.addBindValue(orderId);
    if (!query.exec()) return databaseFailure(query.lastError());
    if (!query.next() || query.value(1).toString() != QStringLiteral("reserved") || query.value(2).toString() != QStringLiteral("reserved")) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("order is not reserved"));
    }
    const int chargerId = query.value(0).toInt();
    query.prepare(QStringLiteral("UPDATE orders SET status='cancelled', ended_at=? WHERE id=?"));
    query.addBindValue(nowIso());
    query.addBindValue(orderId);
    if (!query.exec()) return databaseFailure(query.lastError());
    query.prepare(QStringLiteral("UPDATE chargers SET status='idle', updated_at=? WHERE id=?"));
    query.addBindValue(nowIso());
    query.addBindValue(chargerId);
    if (!query.exec()) return databaseFailure(query.lastError());
    if (!database.commit()) return databaseFailure(database.lastError());
    if (responseData) {
        responseData->insert(QStringLiteral("order"), orderObject(orderId));
    }
    return Result::success();
}

QJsonObject UserService::userObject(int userId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id, mobile, nickname, avatar_path, balance_fen, status, registered_at FROM users WHERE id=?"));
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return {{QStringLiteral("userId"), query.value(0).toInt()},
            {QStringLiteral("mobile"), query.value(1).toString()},
            {QStringLiteral("nickname"), query.value(2).toString()},
            {QStringLiteral("avatarPath"), query.value(3).toString()},
            {QStringLiteral("balanceFen"), query.value(4).toLongLong()},
            {QStringLiteral("status"), query.value(5).toString()},
            {QStringLiteral("registeredAt"), query.value(6).toString()}};
}

QJsonObject UserService::stationObject(int stationId, bool includeDistance, double latitude, double longitude) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT s.id, s.name, s.address, s.latitude, s.longitude, s.price_fen_per_kwh, s.forecast_enabled, "
        "COUNT(c.id), SUM(CASE WHEN c.status='idle' THEN 1 ELSE 0 END) "
        "FROM stations s LEFT JOIN chargers c ON c.station_id=s.id WHERE s.id=? GROUP BY s.id"));
    query.addBindValue(stationId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    QJsonObject station{{QStringLiteral("stationId"), query.value(0).toInt()},
                        {QStringLiteral("name"), query.value(1).toString()},
                        {QStringLiteral("address"), query.value(2).toString()},
                        {QStringLiteral("latitude"), query.value(3).toDouble()},
                        {QStringLiteral("longitude"), query.value(4).toDouble()},
                        {QStringLiteral("priceFenPerKwh"), query.value(5).toInt()},
                        {QStringLiteral("forecastEnabled"), query.value(6).toInt() == 1},
                        {QStringLiteral("chargerCount"), query.value(7).toInt()},
                        {QStringLiteral("idleCount"), query.value(8).toInt()}};
    if (includeDistance) {
        station.insert(QStringLiteral("distanceKm"), distanceKm(latitude, longitude, query.value(3).toDouble(), query.value(4).toDouble()));
    }
    return station;
}

QJsonObject UserService::chargerObject(int chargerId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id, station_id, code, type, power_kw, status, charge_count, total_duration_sec, updated_at FROM chargers WHERE id=?"));
    query.addBindValue(chargerId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return {{QStringLiteral("chargerId"), query.value(0).toInt()},
            {QStringLiteral("stationId"), query.value(1).toInt()},
            {QStringLiteral("code"), query.value(2).toString()},
            {QStringLiteral("type"), query.value(3).toString()},
            {QStringLiteral("powerKw"), query.value(4).toDouble()},
            {QStringLiteral("status"), query.value(5).toString()},
            {QStringLiteral("chargeCount"), query.value(6).toInt()},
            {QStringLiteral("totalDurationSec"), query.value(7).toInt()},
            {QStringLiteral("updatedAt"), query.value(8).toString()}};
}

QJsonObject UserService::orderObject(int orderId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT o.id, o.user_id, o.charger_id, c.station_id, s.name, c.code, o.status, o.reserved_at, "
        "COALESCE(o.started_at, ''), COALESCE(o.ended_at, ''), o.energy_kwh, o.amount_fen "
        "FROM orders o JOIN chargers c ON c.id=o.charger_id JOIN stations s ON s.id=c.station_id WHERE o.id=?"));
    query.addBindValue(orderId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return {{QStringLiteral("orderId"), query.value(0).toInt()},
            {QStringLiteral("userId"), query.value(1).toInt()},
            {QStringLiteral("chargerId"), query.value(2).toInt()},
            {QStringLiteral("stationId"), query.value(3).toInt()},
            {QStringLiteral("stationName"), query.value(4).toString()},
            {QStringLiteral("chargerCode"), query.value(5).toString()},
            {QStringLiteral("status"), query.value(6).toString()},
            {QStringLiteral("reservedAt"), query.value(7).toString()},
            {QStringLiteral("startedAt"), nullableText(query.value(8).toString())},
            {QStringLiteral("endedAt"), nullableText(query.value(9).toString())},
            {QStringLiteral("energyKwh"), query.value(10).toDouble()},
            {QStringLiteral("amountFen"), query.value(11).toLongLong()},
            {QStringLiteral("elapsedSec"), BusinessTime::elapsed(query.value(8).toString(), query.value(9).toString())}};
}

Result UserService::requirePositiveId(const QJsonObject &payload, const QString &field, int *value) const
{
    if (!isInteger(payload.value(field), value)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), field + QStringLiteral(" is invalid"));
    }
    return Result::success();
}

bool UserService::userFrozen(int userId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT status FROM users WHERE id=?"));
    query.addBindValue(userId);
    return query.exec() && query.next() && query.value(0).toString() == QStringLiteral("frozen");
}

int UserService::activeOrderId(int userId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM orders WHERE user_id=? AND status IN ('reserved','charging') ORDER BY id DESC LIMIT 1"));
    query.addBindValue(userId);
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}

int UserService::orderOwner(int orderId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT user_id FROM orders WHERE id=?"));
    query.addBindValue(orderId);
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}
