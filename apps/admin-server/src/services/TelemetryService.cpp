#include "services/TelemetryService.h"
#include "core/BusinessTime.h"
#include "db/SqlTransaction.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <cmath>

namespace {

QString nowIso()
{
    return BusinessTime::now();
}

using BusinessTime::timestampKey;

QJsonValue nullableText(const QString &value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
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

bool isNonNegativeNumber(const QJsonValue &value, double *out)
{
    if (!value.isDouble()) {
        return false;
    }
    *out = value.toDouble();
    return std::isfinite(*out) && *out >= 0.0;
}

bool insertTelemetry(QSqlDatabase database, int chargerId, const QString &recordedAt, double powerKw, double energy, const QString &type)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO telemetry(charger_id, recorded_at, power_kw, energy_increment_kwh, event_type) VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(chargerId);
    query.addBindValue(recordedAt);
    query.addBindValue(powerKw);
    query.addBindValue(energy);
    query.addBindValue(type);
    return query.exec();
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

} // namespace

TelemetryService::TelemetryService(QSqlDatabase database)
    : m_database(database)
{
}

Result TelemetryService::telemetryPush(const QJsonObject &payload, QJsonObject *responseData) const
{
    int chargerId = 0;
    double powerKw = 0.0;
    double energyIncrement = 0.0;
    const QString recordedAt = payload.value(QStringLiteral("recordedAt")).toString();
    const QString status = payload.value(QStringLiteral("status")).toString();
    if (!requirePositiveId(payload, QStringLiteral("chargerId"), &chargerId).ok
        || !validTimestamp(recordedAt)
        || !isNonNegativeNumber(payload.value(QStringLiteral("powerKw")), &powerKw)
        || !isNonNegativeNumber(payload.value(QStringLiteral("energyIncrementKwh")), &energyIncrement)
        || status.isEmpty()) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("telemetry payload is invalid"));
    }
    const QString lastEvent = lastDeviceEventAt(chargerId);
    if (!lastEvent.isEmpty() && timestampKey(recordedAt) <= timestampKey(lastEvent)) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("device event timestamp is stale"));
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT status FROM chargers WHERE id=?"));
    query.addBindValue(chargerId);
    if (!query.exec() || !query.next()) {
        return Result::failure(QStringLiteral("CHARGER_NOT_AVAILABLE"), QStringLiteral("charger not found"));
    }
    const QString currentStatus = query.value(0).toString();
    if (currentStatus != status) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("charger status mismatch"));
    }

    SqlTransaction database(m_database);
    if (!database.transaction()) {
        return databaseFailure(database.lastError());
    }
    if (!insertTelemetry(database, chargerId, recordedAt, powerKw, energyIncrement, QStringLiteral("telemetry"))) {
        database.rollback();
        return databaseFailure(database.lastError());
    }

    const int orderId = activeOrderForCharger(chargerId);
    if (currentStatus == QStringLiteral("charging") && orderId > 0) {
        query.prepare(QStringLiteral(
            "UPDATE orders SET energy_kwh=energy_kwh+?, "
            "amount_fen=CAST((energy_kwh+?) * (SELECT s.price_fen_per_kwh FROM chargers c JOIN stations s ON s.id=c.station_id WHERE c.id=orders.charger_id) + 0.5 AS INTEGER) "
            "WHERE id=? AND status='charging' AND ended_at IS NULL"));
        query.addBindValue(energyIncrement);
        query.addBindValue(energyIncrement);
        query.addBindValue(orderId);
        if (!query.exec()) {
            database.rollback();
            return databaseFailure(query.lastError());
        }
    }
    if (!database.commit()) {
        return databaseFailure(database.lastError());
    }

    if (responseData) {
        responseData->insert(QStringLiteral("acceptedAt"), nowIso());
        responseData->insert(QStringLiteral("order"), orderId > 0 ? QJsonValue(orderObject(orderId)) : QJsonValue(QJsonValue::Null));
    }
    return Result::success();
}

Result TelemetryService::faultSet(const QJsonObject &payload, QJsonObject *responseData) const
{
    int chargerId = 0;
    const QString recordedAt = payload.value(QStringLiteral("recordedAt")).toString();
    if (!requirePositiveId(payload, QStringLiteral("chargerId"), &chargerId).ok
        || !payload.value(QStringLiteral("fault")).isBool()
        || !validTimestamp(recordedAt)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("fault payload is invalid"));
    }
    const bool fault = payload.value(QStringLiteral("fault")).toBool();
    const QString lastEvent = lastDeviceEventAt(chargerId);
    if (!lastEvent.isEmpty() && timestampKey(recordedAt) <= timestampKey(lastEvent)) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("device event timestamp is stale"));
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT status FROM chargers WHERE id=?"));
    query.addBindValue(chargerId);
    if (!query.exec() || !query.next()) {
        return Result::failure(QStringLiteral("CHARGER_NOT_AVAILABLE"), QStringLiteral("charger not found"));
    }
    const QString currentStatus = query.value(0).toString();
    if ((fault && currentStatus != QStringLiteral("idle") && currentStatus != QStringLiteral("reserved") && currentStatus != QStringLiteral("charging")) || (!fault && currentStatus != QStringLiteral("fault"))) {
        return Result::failure(QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("fault intent does not match charger state"));
    }

    SqlTransaction database(m_database);
    if (!database.transaction()) {
        return databaseFailure(database.lastError());
    }
    if (!insertTelemetry(database, chargerId, recordedAt, 0.0, 0.0, QStringLiteral("fault"))) {
        database.rollback();
        return databaseFailure(database.lastError());
    }
    const int orderId = activeOrderForCharger(chargerId);
    if (fault) {
        if (currentStatus == QStringLiteral("reserved") && orderId > 0) {
            query.prepare(QStringLiteral("UPDATE orders SET status='cancelled', ended_at=? WHERE id=? AND status='reserved'"));
            query.addBindValue(recordedAt);
            query.addBindValue(orderId);
            if (!query.exec()) {
                database.rollback();
                return databaseFailure(query.lastError());
            }
        } else if (currentStatus == QStringLiteral("charging") && orderId > 0) {
            query.prepare(QStringLiteral("UPDATE orders SET ended_at=?, amount_fen=CAST(energy_kwh * (SELECT s.price_fen_per_kwh FROM chargers c JOIN stations s ON s.id=c.station_id WHERE c.id=orders.charger_id) + 0.5 AS INTEGER) WHERE id=? AND status='charging' AND ended_at IS NULL"));
            query.addBindValue(recordedAt);
            query.addBindValue(orderId);
            if (!query.exec()) {
                database.rollback();
                return databaseFailure(query.lastError());
            }
        }
        query.prepare(QStringLiteral("UPDATE chargers SET status='fault', updated_at=? WHERE id=?"));
        query.addBindValue(recordedAt);
        query.addBindValue(chargerId);
        if (!query.exec()) {
            database.rollback();
            return databaseFailure(query.lastError());
        }
    }
    if (!insertEvent(database, QStringLiteral("simulator.fault_set"), QStringLiteral("charger"), chargerId, fault ? QStringLiteral("fault") : QStringLiteral("recover intent"))) {
        database.rollback();
        return databaseFailure(database.lastError());
    }
    if (!database.commit()) {
        return databaseFailure(database.lastError());
    }
    if (responseData) {
        responseData->insert(QStringLiteral("charger"), chargerObject(chargerId));
    }
    return Result::success();
}

Result TelemetryService::simulatorStatus(const QJsonObject &payload, QJsonObject *responseData) const
{
    int eventCount = 0;
    const QString state = payload.value(QStringLiteral("state")).toString();
    const QString simulatedAt = payload.value(QStringLiteral("simulatedAt")).toString();
    if ((state != QStringLiteral("running") && state != QStringLiteral("paused"))
        || !validTimestamp(simulatedAt)
        || !isInteger(payload.value(QStringLiteral("eventCount")), &eventCount, true)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("simulator status payload is invalid"));
    }
    SqlTransaction database(m_database);
    if (!database.transaction()) return databaseFailure(database.lastError());
    if (!insertEvent(m_database, QStringLiteral("simulator.status"), QStringLiteral("simulator"), 0, state)) {
        return databaseFailure(m_database.lastError());
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT id FROM chargers ORDER BY CAST(code AS INTEGER)"))) {
        return databaseFailure(query.lastError());
    }
    QJsonArray chargers;
    while (query.next()) {
        chargers.append(chargerObject(query.value(0).toInt()));
    }
    query.finish();
    if (!database.commit()) return databaseFailure(database.lastError());
    if (responseData) {
        responseData->insert(QStringLiteral("acceptedAt"), nowIso());
        responseData->insert(QStringLiteral("chargers"), chargers);
    }
    return Result::success();
}

QJsonObject TelemetryService::chargerObject(int chargerId) const
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

QJsonObject TelemetryService::orderObject(int orderId) const
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

Result TelemetryService::requirePositiveId(const QJsonObject &payload, const QString &field, int *value) const
{
    if (!isInteger(payload.value(field), value)) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), field + QStringLiteral(" is invalid"));
    }
    return Result::success();
}

int TelemetryService::activeOrderForCharger(int chargerId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM orders WHERE charger_id=? AND status IN ('reserved','charging') ORDER BY id DESC LIMIT 1"));
    query.addBindValue(chargerId);
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}

bool TelemetryService::validTimestamp(const QString &value) const
{
    return !timestampKey(value).isEmpty();
}

QString TelemetryService::lastDeviceEventAt(int chargerId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT recorded_at FROM telemetry WHERE charger_id=?"));
    query.addBindValue(chargerId);
    if (!query.exec()) return {};
    QString latest;
    QString latestKey;
    while (query.next()) {
        const QString value = query.value(0).toString();
        const QString key = timestampKey(value);
        if (key > latestKey) {
            latest = value;
            latestKey = key;
        }
    }
    return latest;
}
