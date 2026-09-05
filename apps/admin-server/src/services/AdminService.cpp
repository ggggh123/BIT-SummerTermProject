#include "services/AdminService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>
#include <cmath>

namespace {

QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

bool isInteger(const QJsonValue &value, int *out, bool allowZero = false)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number) {
        return false;
    }
    *out = value.toInt();
    return allowZero ? *out >= 0 : *out > 0;
}

bool pageArgs(const QJsonObject &payload, int *limit, int *offset)
{
    *limit = payload.value(QStringLiteral("limit")).toInt(20);
    *offset = payload.value(QStringLiteral("offset")).toInt(0);
    return *limit >= 1 && *limit <= 100 && *offset >= 0;
}

bool bumpSnapshotVersion(QSqlDatabase database)
{
    QSqlQuery query(database);
    return query.exec(QStringLiteral("UPDATE snapshot_meta SET version=version+1 WHERE id=1"));
}

bool insertEvent(QSqlDatabase database, const QString &type, const QString &entityType, int entityId, const QString &message)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO events(event_type, entity_type, entity_id, message, created_at) VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(type);
    query.addBindValue(entityType);
    query.addBindValue(entityId);
    query.addBindValue(message);
    query.addBindValue(nowIso());
    return query.exec();
}

int nextChargerCode(QSqlDatabase database)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT COALESCE(MAX(CAST(code AS INTEGER)), 1000) + 1 FROM chargers")) || !query.next()) {
        return 1001;
    }
    return query.value(0).toInt();
}

} // namespace

AdminService::AdminService(QSqlDatabase database)
    : m_database(database)
{
}

Result AdminService::stationCreate(const QJsonObject &payload, QJsonObject *responseData) const
{
    const QString name = payload.value(QStringLiteral("name")).toString().trimmed();
    const QString address = payload.value(QStringLiteral("address")).toString().trimmed();
    const double latitude = payload.value(QStringLiteral("latitude")).toDouble();
    const double longitude = payload.value(QStringLiteral("longitude")).toDouble();
    int priceFenPerKwh = 0;
    int fastCount = 0;
    int slowCount = 0;

    if (name.isEmpty() || address.isEmpty()
        || !std::isfinite(latitude) || latitude < -90.0 || latitude > 90.0
        || !std::isfinite(longitude) || longitude < -180.0 || longitude > 180.0
        || !isInteger(payload.value(QStringLiteral("priceFenPerKwh")), &priceFenPerKwh)
        || !isInteger(payload.value(QStringLiteral("fastChargerCount")), &fastCount, true)
        || !isInteger(payload.value(QStringLiteral("slowChargerCount")), &slowCount, true)
        || fastCount + slowCount < 1) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("station payload is invalid"));
    }

    QSqlDatabase database = m_database;
    if (!database.transaction()) {
        return Result::failure(QStringLiteral("DB_BUSY"), database.lastError().text());
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO stations(name, address, latitude, longitude, price_fen_per_kwh, forecast_enabled, created_at) "
        "VALUES(?, ?, ?, ?, ?, 0, ?)"));
    query.addBindValue(name);
    query.addBindValue(address);
    query.addBindValue(latitude);
    query.addBindValue(longitude);
    query.addBindValue(priceFenPerKwh);
    query.addBindValue(nowIso());
    if (!query.exec()) {
        database.rollback();
        return Result::failure(QStringLiteral("DB_BUSY"), query.lastError().text());
    }
    const int stationId = query.lastInsertId().toInt();
    int code = nextChargerCode(database);
    QJsonArray chargers;
    for (int i = 0; i < fastCount + slowCount; ++i) {
        const bool fast = i < fastCount;
        query.prepare(QStringLiteral(
            "INSERT INTO chargers(station_id, code, type, power_kw, status, charge_count, total_duration_sec, updated_at) "
            "VALUES(?, ?, ?, ?, 'idle', 0, 0, ?)"));
        query.addBindValue(stationId);
        query.addBindValue(QString::number(code++));
        query.addBindValue(fast ? QStringLiteral("fast") : QStringLiteral("slow"));
        query.addBindValue(fast ? 60.0 : 30.0);
        query.addBindValue(nowIso());
        if (!query.exec()) {
            database.rollback();
            return Result::failure(QStringLiteral("DB_BUSY"), query.lastError().text());
        }
        chargers.append(chargerObject(query.lastInsertId().toInt()));
    }
    if (!bumpSnapshotVersion(database)
        || !insertEvent(database, QStringLiteral("admin.station_create"), QStringLiteral("station"), stationId, QStringLiteral("station created"))) {
        database.rollback();
        return Result::failure(QStringLiteral("DB_BUSY"), database.lastError().text());
    }
    if (!database.commit()) {
        return Result::failure(QStringLiteral("DB_BUSY"), database.lastError().text());
    }

    if (responseData) {
        responseData->insert(QStringLiteral("station"), stationObject(stationId));
        responseData->insert(QStringLiteral("chargers"), chargers);
    }
    return Result::success();
}

Result AdminService::chargerRestart(const QJsonObject &payload, QJsonObject *responseData) const
{
    int chargerId = 0;
    const Result idResult = requirePositiveId(payload, QStringLiteral("chargerId"), &chargerId);
    if (!idResult.ok) {
        return idResult;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT status FROM chargers WHERE id=?"));
    query.addBindValue(chargerId);
    if (!query.exec() || !query.next()) {
        return Result::failure(QStringLiteral("CHARGER_NOT_AVAILABLE"), QStringLiteral("charger not found"));
    }
    if (query.value(0).toString() != QStringLiteral("fault")) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("charger is not fault"));
    }

    QSqlDatabase database = m_database;
    if (!database.transaction()) {
        return Result::failure(QStringLiteral("DB_BUSY"), database.lastError().text());
    }
    query.prepare(QStringLiteral("UPDATE chargers SET status='restarting', updated_at=? WHERE id=? AND status='fault'"));
    query.addBindValue(nowIso());
    query.addBindValue(chargerId);
    if (!query.exec() || query.numRowsAffected() != 1
        || !bumpSnapshotVersion(database)
        || !insertEvent(database, QStringLiteral("admin.charger_restart"), QStringLiteral("charger"), chargerId, QStringLiteral("charger restarting"))) {
        database.rollback();
        return Result::failure(QStringLiteral("DB_BUSY"), query.lastError().text());
    }
    if (!database.commit()) {
        return Result::failure(QStringLiteral("DB_BUSY"), database.lastError().text());
    }
    QTimer::singleShot(1500, [this, chargerId]() {
        finishRestart(chargerId);
    });
    if (responseData) {
        responseData->insert(QStringLiteral("charger"), chargerObject(chargerId));
    }
    return Result::success();
}

Result AdminService::finishRestart(int chargerId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE chargers SET status='idle', updated_at=? WHERE id=? AND status='restarting'"));
    query.addBindValue(nowIso());
    query.addBindValue(chargerId);
    if (!query.exec()) {
        return Result::failure(QStringLiteral("DB_BUSY"), query.lastError().text());
    }
    if (query.numRowsAffected() == 0) {
        return Result::success();
    }
    bumpSnapshotVersion(m_database);
    insertEvent(m_database, QStringLiteral("admin.charger_restart.done"), QStringLiteral("charger"), chargerId, QStringLiteral("charger idle"));
    return Result::success();
}

Result AdminService::userList(const QJsonObject &payload, QJsonObject *responseData) const
{
    const QString mobileLike = payload.value(QStringLiteral("mobileLike")).toString();
    for (const QChar ch : mobileLike) {
        if (!ch.isDigit()) {
            return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("mobileLike is invalid"));
        }
    }
    if (mobileLike.size() > 11) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("mobileLike is invalid"));
    }

    int limit = 20;
    int offset = 0;
    if (!pageArgs(payload, &limit, &offset)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("pagination is invalid"));
    }

    const QString filter = QStringLiteral("%") + mobileLike + QStringLiteral("%");
    QSqlQuery countQuery(m_database);
    countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE mobile LIKE ?"));
    countQuery.addBindValue(filter);
    if (!countQuery.exec() || !countQuery.next()) {
        return Result::failure(QStringLiteral("DB_BUSY"), countQuery.lastError().text());
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM users WHERE mobile LIKE ? ORDER BY id LIMIT ? OFFSET ?"));
    query.addBindValue(filter);
    query.addBindValue(limit);
    query.addBindValue(offset);
    if (!query.exec()) {
        return Result::failure(QStringLiteral("DB_BUSY"), query.lastError().text());
    }
    QJsonArray items;
    while (query.next()) {
        items.append(userObject(query.value(0).toInt()));
    }
    if (responseData) {
        responseData->insert(QStringLiteral("items"), items);
        responseData->insert(QStringLiteral("total"), countQuery.value(0).toInt());
    }
    return Result::success();
}

Result AdminService::userSetStatus(const QJsonObject &payload, QJsonObject *responseData) const
{
    int userId = 0;
    const Result idResult = requirePositiveId(payload, QStringLiteral("userId"), &userId);
    const QString status = payload.value(QStringLiteral("status")).toString();
    if (!idResult.ok || (status != QStringLiteral("active") && status != QStringLiteral("frozen"))) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("user status payload is invalid"));
    }
    if (userObject(userId).isEmpty()) {
        return Result::failure(QStringLiteral("ENTITY_NOT_FOUND"), QStringLiteral("user not found"));
    }

    QSqlDatabase database = m_database;
    if (!database.transaction()) {
        return Result::failure(QStringLiteral("DB_BUSY"), database.lastError().text());
    }
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE users SET status=? WHERE id=?"));
    query.addBindValue(status);
    query.addBindValue(userId);
    if (!query.exec()
        || !bumpSnapshotVersion(database)
        || !insertEvent(database, QStringLiteral("admin.user_set_status"), QStringLiteral("user"), userId, status)) {
        database.rollback();
        return Result::failure(QStringLiteral("DB_BUSY"), query.lastError().text());
    }
    if (!database.commit()) {
        return Result::failure(QStringLiteral("DB_BUSY"), database.lastError().text());
    }
    if (responseData) {
        responseData->insert(QStringLiteral("user"), userObject(userId));
    }
    return Result::success();
}

QJsonObject AdminService::stationObject(int stationId) const
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
    return {{QStringLiteral("stationId"), query.value(0).toInt()},
            {QStringLiteral("name"), query.value(1).toString()},
            {QStringLiteral("address"), query.value(2).toString()},
            {QStringLiteral("latitude"), query.value(3).toDouble()},
            {QStringLiteral("longitude"), query.value(4).toDouble()},
            {QStringLiteral("priceFenPerKwh"), query.value(5).toInt()},
            {QStringLiteral("forecastEnabled"), query.value(6).toInt() == 1},
            {QStringLiteral("chargerCount"), query.value(7).toInt()},
            {QStringLiteral("idleCount"), query.value(8).toInt()}};
}

QJsonObject AdminService::chargerObject(int chargerId) const
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

QJsonObject AdminService::userObject(int userId) const
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

Result AdminService::requirePositiveId(const QJsonObject &payload, const QString &field, int *value) const
{
    if (!isInteger(payload.value(field), value)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), field + QStringLiteral(" is invalid"));
    }
    return Result::success();
}
