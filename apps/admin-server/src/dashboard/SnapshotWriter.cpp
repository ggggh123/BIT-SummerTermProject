#include "dashboard/SnapshotWriter.h"
#include "core/BusinessTime.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QIODevice>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>

namespace {

QJsonObject emptyChargerStatus()
{
    return {
        {QStringLiteral("idle"), 0},
        {QStringLiteral("reserved"), 0},
        {QStringLiteral("charging"), 0},
        {QStringLiteral("fault"), 0},
        {QStringLiteral("restarting"), 0},
        {QStringLiteral("total"), 0}
    };
}

} // namespace

SnapshotWriter::SnapshotWriter(QSqlDatabase database)
    : m_database(database)
{
}

bool SnapshotWriter::write(const QString &path, QString *errorMessage) const
{
    const QFileInfo fileInfo(path);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to create snapshot directory: %1").arg(fileInfo.absolutePath());
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const QByteArray bytes = QJsonDocument(buildSnapshot()).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    return true;
}

QJsonObject SnapshotWriter::buildSnapshot() const
{
    QJsonObject snapshot;
    snapshot.insert(QStringLiteral("schemaVersion"), 1);
    snapshot.insert(QStringLiteral("generatedAt"), BusinessTime::now());
    snapshot.insert(QStringLiteral("snapshotVersion"), scalarInt(QStringLiteral("SELECT version FROM snapshot_meta WHERE id=1")));

    QJsonObject kpi;
    kpi.insert(QStringLiteral("stationCount"), scalarInt(QStringLiteral("SELECT COUNT(*) FROM stations")));
    kpi.insert(QStringLiteral("chargerCount"), scalarInt(QStringLiteral("SELECT COUNT(*) FROM chargers")));
    kpi.insert(QStringLiteral("userCount"), scalarInt(QStringLiteral("SELECT COUNT(*) FROM users")));
    kpi.insert(QStringLiteral("orderCount"), scalarInt(QStringLiteral("SELECT COUNT(*) FROM orders")));
    kpi.insert(QStringLiteral("totalRevenueFen"), scalarInt(QStringLiteral("SELECT COALESCE(SUM(amount_fen), 0) FROM orders WHERE status='completed'")));
    snapshot.insert(QStringLiteral("kpi"), kpi);

    QJsonObject chargerStatus = emptyChargerStatus();
    QSqlQuery statusQuery(m_database);
    if (statusQuery.exec(QStringLiteral("SELECT status, COUNT(*) FROM chargers GROUP BY status"))) {
        int total = 0;
        while (statusQuery.next()) {
            const QString status = statusQuery.value(0).toString();
            const int count = statusQuery.value(1).toInt();
            chargerStatus.insert(status, count);
            total += count;
        }
        chargerStatus.insert(QStringLiteral("total"), total);
    }
    snapshot.insert(QStringLiteral("chargerStatus"), chargerStatus);

    QJsonArray stationRanking;
    QSqlQuery stationQuery(m_database);
    if (stationQuery.exec(QStringLiteral(
            "SELECT s.id, s.name, s.address, "
            "COUNT(c.id) AS total_count, "
            "SUM(CASE WHEN c.status='idle' THEN 1 ELSE 0 END) AS idle_count, "
            "SUM(CASE WHEN c.status='charging' THEN 1 ELSE 0 END) AS charging_count, "
            "SUM(CASE WHEN c.status='fault' THEN 1 ELSE 0 END) AS fault_count "
            "FROM stations s LEFT JOIN chargers c ON c.station_id=s.id "
            "GROUP BY s.id, s.name, s.address ORDER BY s.id"))) {
        while (stationQuery.next()) {
            const int total = stationQuery.value(3).toInt();
            const int fault = stationQuery.value(6).toInt();
            QJsonObject station;
            station.insert(QStringLiteral("stationId"), stationQuery.value(0).toInt());
            station.insert(QStringLiteral("name"), stationQuery.value(1).toString());
            station.insert(QStringLiteral("address"), stationQuery.value(2).toString());
            station.insert(QStringLiteral("chargerCount"), total);
            station.insert(QStringLiteral("idleCount"), stationQuery.value(4).toInt());
            station.insert(QStringLiteral("chargingCount"), stationQuery.value(5).toInt());
            station.insert(QStringLiteral("faultCount"), fault);
            station.insert(QStringLiteral("onlineRate"), total == 0 ? 0.0 : (total - fault) * 1.0 / total);
            stationRanking.append(station);
        }
    }
    snapshot.insert(QStringLiteral("stationRanking"), stationRanking);

    QString activeRunId;
    QSqlQuery runQuery(m_database);
    if (runQuery.exec(QStringLiteral(
            "SELECT run_id, generated_at, data_cutoff, activated_at, model_version, payload_hash "
            "FROM forecast_runs WHERE status='active' ORDER BY activated_at DESC LIMIT 1")) && runQuery.next()) {
        activeRunId = runQuery.value(0).toString();
        QJsonObject run;
        run.insert(QStringLiteral("runId"), activeRunId);
        run.insert(QStringLiteral("generatedAt"), runQuery.value(1).toString());
        run.insert(QStringLiteral("dataCutoff"), runQuery.value(2).toString());
        run.insert(QStringLiteral("activatedAt"), runQuery.value(3).toString());
        run.insert(QStringLiteral("modelVersion"), runQuery.value(4).toString());
        run.insert(QStringLiteral("payloadHash"), runQuery.value(5).toString());
        run.insert(QStringLiteral("stale"), false);
        snapshot.insert(QStringLiteral("forecastRun"), run);
    } else {
        snapshot.insert(QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null));
    }

    QJsonArray forecast24h;
    if (!activeRunId.isEmpty()) {
        QSqlQuery forecastQuery(m_database);
        forecastQuery.prepare(QStringLiteral(
            "SELECT station_id, forecast_at, horizon_h, predicted_load_kw, predicted_busy_count, "
            "predicted_idle_count, congestion_level, is_peak "
            "FROM forecasts WHERE run_id=? ORDER BY station_id, horizon_h"));
        forecastQuery.addBindValue(activeRunId);
        if (forecastQuery.exec()) {
            while (forecastQuery.next()) {
                QJsonObject forecast;
                forecast.insert(QStringLiteral("stationId"), forecastQuery.value(0).toInt());
                forecast.insert(QStringLiteral("forecastAt"), forecastQuery.value(1).toString());
                forecast.insert(QStringLiteral("horizonH"), forecastQuery.value(2).toInt());
                forecast.insert(QStringLiteral("predictedLoadKw"), forecastQuery.value(3).toDouble());
                forecast.insert(QStringLiteral("predictedBusyCount"), forecastQuery.value(4).toInt());
                forecast.insert(QStringLiteral("predictedIdleCount"), forecastQuery.value(5).toInt());
                forecast.insert(QStringLiteral("congestionLevel"), forecastQuery.value(6).toString());
                forecast.insert(QStringLiteral("isPeak"), forecastQuery.value(7).toInt() == 1);
                forecast24h.append(forecast);
            }
        }
    }
    snapshot.insert(QStringLiteral("forecast24h"), forecast24h);

    return snapshot;
}

int SnapshotWriter::scalarInt(const QString &sql, int fallback) const
{
    QSqlQuery query(m_database);
    if (query.exec(sql) && query.next()) {
        return query.value(0).toInt();
    }
    return fallback;
}
