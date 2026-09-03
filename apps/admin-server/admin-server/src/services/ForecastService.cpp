#include "services/ForecastService.h"

#include "contracts/Statuses.h"
#include "dashboard/SnapshotWriter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <utility>

namespace {

QJsonValue fieldValue(const QJsonObject &object, const QString &camelName, const QString &snakeName = QString())
{
    if (object.contains(camelName)) {
        return object.value(camelName);
    }
    if (!snakeName.isEmpty() && object.contains(snakeName)) {
        return object.value(snakeName);
    }
    return QJsonValue(QJsonValue::Undefined);
}

QString requiredString(const QJsonObject &object, const QString &fieldName)
{
    return object.value(fieldName).toString().trimmed();
}

QString payloadHash(const QJsonObject &payload)
{
    const QByteArray payloadBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(payloadBytes, QCryptographicHash::Sha256).toHex());
}

bool jsonBool(const QJsonValue &value, bool *ok)
{
    if (value.isBool()) {
        *ok = true;
        return value.toBool();
    }
    if (value.isDouble()) {
        const int number = value.toInt(-1);
        if (number == 0 || number == 1) {
            *ok = true;
            return number == 1;
        }
    }
    *ok = false;
    return false;
}

QJsonObject runFromQuery(const QSqlQuery &query)
{
    const QString activatedAt = query.value(3).toString();
    const QDateTime activatedTime = QDateTime::fromString(activatedAt, Qt::ISODate);
    const bool stale = activatedTime.isValid() && activatedTime.secsTo(QDateTime::currentDateTime()) > 2 * 60 * 60;

    return {
        {QStringLiteral("runId"), query.value(0).toString()},
        {QStringLiteral("generatedAt"), query.value(1).toString()},
        {QStringLiteral("dataCutoff"), query.value(2).toString()},
        {QStringLiteral("activatedAt"), activatedAt},
        {QStringLiteral("modelVersion"), query.value(4).toString()},
        {QStringLiteral("payloadHash"), query.value(5).toString()},
        {QStringLiteral("stale"), stale}
    };
}

QJsonArray recordsForRun(QSqlDatabase database, const QString &runId)
{
    QJsonArray records;
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT station_id, forecast_at, horizon_h, predicted_load_kw, predicted_busy_count, "
        "predicted_idle_count, congestion_level, is_peak "
        "FROM forecasts WHERE run_id=? ORDER BY station_id, horizon_h"));
    query.addBindValue(runId);
    if (!query.exec()) {
        return records;
    }

    while (query.next()) {
        records.append(QJsonObject{
            {QStringLiteral("stationId"), query.value(0).toInt()},
            {QStringLiteral("forecastAt"), query.value(1).toString()},
            {QStringLiteral("horizonH"), query.value(2).toInt()},
            {QStringLiteral("predictedLoadKw"), query.value(3).toDouble()},
            {QStringLiteral("predictedBusyCount"), query.value(4).toInt()},
            {QStringLiteral("predictedIdleCount"), query.value(5).toInt()},
            {QStringLiteral("congestionLevel"), query.value(6).toString()},
            {QStringLiteral("isPeak"), query.value(7).toInt() == 1}
        });
    }
    return records;
}

Result validateRecord(const QJsonObject &record, QSet<QString> *keys, QHash<int, QSet<int>> *horizonsByStation)
{
    const int stationId = fieldValue(record, QStringLiteral("stationId"), QStringLiteral("station_id")).toInt(0);
    const int horizonH = fieldValue(record, QStringLiteral("horizonH"), QStringLiteral("horizon_h")).toInt(0);
    const QString forecastAt = fieldValue(record, QStringLiteral("forecastAt"), QStringLiteral("forecast_at")).toString().trimmed();
    const double predictedLoadKw = fieldValue(record, QStringLiteral("predictedLoadKw"), QStringLiteral("predicted_load_kw")).toDouble(-1.0);
    const int predictedBusyCount = fieldValue(record, QStringLiteral("predictedBusyCount"), QStringLiteral("predicted_busy_count")).toInt(-1);
    const int predictedIdleCount = fieldValue(record, QStringLiteral("predictedIdleCount"), QStringLiteral("predicted_idle_count")).toInt(-1);
    const QString congestionLevel = fieldValue(record, QStringLiteral("congestionLevel"), QStringLiteral("congestion_level")).toString();

    bool peakOk = false;
    jsonBool(fieldValue(record, QStringLiteral("isPeak"), QStringLiteral("is_peak")), &peakOk);

    if (stationId <= 0 || horizonH < 1 || horizonH > 24 || forecastAt.isEmpty()
        || predictedLoadKw < 0.0 || predictedBusyCount < 0 || predictedIdleCount < 0
        || !ev::status::isCongestion(congestionLevel) || !peakOk) {
        return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("forecast record fields are invalid"));
    }

    const QString key = QStringLiteral("%1:%2").arg(stationId).arg(horizonH);
    if (keys->contains(key)) {
        return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("forecast records contain duplicate stationId/horizonH"));
    }
    keys->insert(key);
    (*horizonsByStation)[stationId].insert(horizonH);

    return Result::success();
}

} // namespace

ForecastService::ForecastService(QSqlDatabase database, QString snapshotPath)
    : m_database(database)
    , m_snapshotPath(std::move(snapshotPath))
{
}

QJsonObject ForecastService::healthState() const
{
    int snapshotVersion = 0;
    QSqlQuery metaQuery(m_database);
    if (metaQuery.exec(QStringLiteral("SELECT version FROM snapshot_meta WHERE id=1")) && metaQuery.next()) {
        snapshotVersion = metaQuery.value(0).toInt();
    }

    QString runId;
    int acceptedCount = 0;
    QSqlQuery runQuery(m_database);
    if (runQuery.exec(QStringLiteral(
            "SELECT r.run_id, COUNT(f.run_id) FROM forecast_runs r "
            "LEFT JOIN forecasts f ON f.run_id=r.run_id "
            "WHERE r.status='active' GROUP BY r.run_id LIMIT 1")) && runQuery.next()) {
        runId = runQuery.value(0).toString();
        acceptedCount = runQuery.value(1).toInt();
    }

    return {
        {QStringLiteral("status"), acceptedCount == 144 ? QStringLiteral("ready") : QStringLiteral("degraded")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("snapshotVersion"), snapshotVersion},
        {QStringLiteral("forecastRunId"), runId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(runId)},
        {QStringLiteral("serverTime"), QDateTime::currentDateTime().toString(Qt::ISODate)},
        {QStringLiteral("service"), QStringLiteral("charging-platform-admin-server")}
    };
}

Result ForecastService::latest(QJsonObject *responseData) const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT run_id, generated_at, data_cutoff, activated_at, model_version, payload_hash "
            "FROM forecast_runs WHERE status='active' ORDER BY activated_at DESC LIMIT 1"))) {
        return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
    }

    if (!query.next()) {
        if (responseData) {
            responseData->insert(QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null));
            responseData->insert(QStringLiteral("records"), QJsonArray{});
        }
        return Result::success();
    }

    const QString runId = query.value(0).toString();
    const QJsonArray records = recordsForRun(m_database, runId);
    if (records.size() != 144) {
        return Result::failure(QStringLiteral("DB_ERROR"), QStringLiteral("active forecast run does not contain 144 records"));
    }

    if (responseData) {
        responseData->insert(QStringLiteral("forecastRun"), runFromQuery(query));
        responseData->insert(QStringLiteral("records"), records);
    }
    return Result::success();
}

Result ForecastService::publish(const QString &requestId, const QJsonObject &payload, QJsonObject *responseData) const
{
    Q_UNUSED(requestId)

    QString runId;
    Result result = validatePayload(payload, &runId);
    if (!result.ok) {
        return result;
    }

    const QString currentPayloadHash = payloadHash(payload);
    const QString acceptedHash = acceptedActiveRunHash(runId);
    QString snapshotError;
    if (!acceptedHash.isEmpty()) {
        if (acceptedHash != currentPayloadHash) {
            return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("runId has already been accepted with a different payload"));
        }
        const bool snapshotReady = writeSnapshot(&snapshotError);
        if (responseData) {
            responseData->insert(QStringLiteral("runId"), runId);
            responseData->insert(QStringLiteral("acceptedCount"), 144);
            responseData->insert(QStringLiteral("snapshotReady"), snapshotReady);
            if (!snapshotReady) {
                responseData->insert(QStringLiteral("warning"), snapshotError);
            }
        }
        return Result::success(QStringLiteral("forecast accepted"));
    }

    result = insertForecastRun(payload, runId);
    if (!result.ok) {
        return result;
    }

    const bool snapshotReady = writeSnapshot(&snapshotError);
    if (responseData) {
        responseData->insert(QStringLiteral("runId"), runId);
        responseData->insert(QStringLiteral("acceptedCount"), 144);
        responseData->insert(QStringLiteral("snapshotReady"), snapshotReady);
        if (!snapshotReady) {
            responseData->insert(QStringLiteral("warning"), snapshotError);
        }
    }

    return Result::success(QStringLiteral("forecast accepted"));
}

Result ForecastService::validatePayload(const QJsonObject &payload, QString *runId) const
{
    const QString id = requiredString(payload, QStringLiteral("runId"));
    if (id.isEmpty()) {
        return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("runId is required"));
    }
    const QString generatedAtText = requiredString(payload, QStringLiteral("generatedAt"));
    const QString dataCutoffText = requiredString(payload, QStringLiteral("dataCutoff"));
    if (generatedAtText.isEmpty() || dataCutoffText.isEmpty()
        || requiredString(payload, QStringLiteral("modelVersion")).isEmpty()) {
        return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("generatedAt, dataCutoff and modelVersion are required"));
    }

    const QDateTime generatedAt = QDateTime::fromString(generatedAtText, Qt::ISODate);
    const QDateTime dataCutoff = QDateTime::fromString(dataCutoffText, Qt::ISODate);
    if (!generatedAt.isValid() || !dataCutoff.isValid() || dataCutoff > generatedAt) {
        return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("dataCutoff must be earlier than or equal to generatedAt"));
    }

    const QJsonValue recordsValue = payload.value(QStringLiteral("records"));
    if (!recordsValue.isArray()) {
        return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("records must be an array"));
    }

    const QJsonArray records = recordsValue.toArray();
    if (records.size() != 144) {
        return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("forecast.publish requires exactly 144 records"));
    }

    QSet<QString> keys;
    QHash<int, QSet<int>> horizonsByStation;
    for (const QJsonValue &recordValue : records) {
        if (!recordValue.isObject()) {
            return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("each forecast record must be an object"));
        }
        const Result recordResult = validateRecord(recordValue.toObject(), &keys, &horizonsByStation);
        if (!recordResult.ok) {
            return recordResult;
        }
    }

    QSet<int> enabledStations;
    QSqlQuery stationQuery(m_database);
    if (!stationQuery.exec(QStringLiteral("SELECT id FROM stations WHERE forecast_enabled=1 ORDER BY id"))) {
        return Result::failure(QStringLiteral("DB_ERROR"), stationQuery.lastError().text());
    }
    while (stationQuery.next()) {
        enabledStations.insert(stationQuery.value(0).toInt());
    }

    QSet<int> publishedStations;
    for (auto it = horizonsByStation.cbegin(); it != horizonsByStation.cend(); ++it) {
        publishedStations.insert(it.key());
    }
    if (enabledStations.size() != 6 || publishedStations != enabledStations) {
        return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("records must cover the 6 forecast-enabled stations"));
    }
    for (auto it = horizonsByStation.cbegin(); it != horizonsByStation.cend(); ++it) {
        if (it.value().size() != 24) {
            return Result::failure(QStringLiteral("FORECAST_INVALID"), QStringLiteral("each forecast-enabled station must contain horizonH 1..24"));
        }
    }

    *runId = id;
    return Result::success();
}

Result ForecastService::insertForecastRun(const QJsonObject &payload, const QString &runId) const
{
    const QString currentPayloadHash = payloadHash(payload);
    const QString activatedAt = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSqlDatabase database = m_database;
    if (!database.transaction()) {
        return Result::failure(QStringLiteral("DB_ERROR"), database.lastError().text());
    }

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("UPDATE forecast_runs SET status='superseded' WHERE status='active'"))) {
        database.rollback();
        return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
    }

    query.prepare(QStringLiteral(
        "INSERT INTO forecast_runs(run_id, generated_at, data_cutoff, activated_at, model_version, payload_hash, status) "
        "VALUES(?, ?, ?, ?, ?, ?, 'active')"));
    query.addBindValue(runId);
    query.addBindValue(requiredString(payload, QStringLiteral("generatedAt")));
    query.addBindValue(requiredString(payload, QStringLiteral("dataCutoff")));
    query.addBindValue(activatedAt);
    query.addBindValue(requiredString(payload, QStringLiteral("modelVersion")));
    query.addBindValue(currentPayloadHash);
    if (!query.exec()) {
        database.rollback();
        return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
    }

    const QJsonArray records = payload.value(QStringLiteral("records")).toArray();
    for (const QJsonValue &recordValue : records) {
        const QJsonObject record = recordValue.toObject();
        bool peakOk = false;
        const bool isPeak = jsonBool(fieldValue(record, QStringLiteral("isPeak"), QStringLiteral("is_peak")), &peakOk);
        Q_UNUSED(peakOk)

        QSqlQuery insertForecast(database);
        insertForecast.prepare(QStringLiteral(
            "INSERT INTO forecasts(run_id, station_id, forecast_at, horizon_h, predicted_load_kw, "
            "predicted_busy_count, predicted_idle_count, congestion_level, is_peak) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        insertForecast.addBindValue(runId);
        insertForecast.addBindValue(fieldValue(record, QStringLiteral("stationId"), QStringLiteral("station_id")).toInt());
        insertForecast.addBindValue(fieldValue(record, QStringLiteral("forecastAt"), QStringLiteral("forecast_at")).toString().trimmed());
        insertForecast.addBindValue(fieldValue(record, QStringLiteral("horizonH"), QStringLiteral("horizon_h")).toInt());
        insertForecast.addBindValue(fieldValue(record, QStringLiteral("predictedLoadKw"), QStringLiteral("predicted_load_kw")).toDouble());
        insertForecast.addBindValue(fieldValue(record, QStringLiteral("predictedBusyCount"), QStringLiteral("predicted_busy_count")).toInt());
        insertForecast.addBindValue(fieldValue(record, QStringLiteral("predictedIdleCount"), QStringLiteral("predicted_idle_count")).toInt());
        insertForecast.addBindValue(fieldValue(record, QStringLiteral("congestionLevel"), QStringLiteral("congestion_level")).toString());
        insertForecast.addBindValue(isPeak ? 1 : 0);
        if (!insertForecast.exec()) {
            database.rollback();
            return Result::failure(QStringLiteral("DB_ERROR"), insertForecast.lastError().text());
        }
    }

    if (!query.exec(QStringLiteral("UPDATE snapshot_meta SET version=version+1 WHERE id=1"))) {
        database.rollback();
        return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
    }

    if (!database.commit()) {
        return Result::failure(QStringLiteral("DB_ERROR"), database.lastError().text());
    }

    return Result::success();
}

QString ForecastService::acceptedActiveRunHash(const QString &runId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT r.payload_hash, COUNT(f.run_id) FROM forecast_runs r "
        "LEFT JOIN forecasts f ON f.run_id=r.run_id "
        "WHERE r.run_id=? AND r.status='active' GROUP BY r.run_id, r.payload_hash"));
    query.addBindValue(runId);
    if (query.exec() && query.next() && query.value(1).toInt() == 144) {
        return query.value(0).toString();
    }
    return QString();
}

bool ForecastService::writeSnapshot(QString *errorMessage) const
{
    SnapshotWriter writer(m_database);
    return writer.write(m_snapshotPath, errorMessage);
}





