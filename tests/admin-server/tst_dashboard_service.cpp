#include "services/DashboardService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QTimeZone>
#include <QUuid>

namespace {

class ScopedDatabase
{
public:
    ScopedDatabase()
        : m_name(QStringLiteral("dashboard-service-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
        , m_database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name))
    {
        m_database.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_database.open());
        QSqlQuery query(m_database);
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE chargers (id INTEGER PRIMARY KEY, status TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE stations (id INTEGER PRIMARY KEY, name TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE orders (id INTEGER PRIMARY KEY, status TEXT NOT NULL, amount_fen INTEGER NOT NULL, ended_at TEXT)")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE forecast_runs (run_id TEXT PRIMARY KEY, status TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE forecasts (run_id TEXT NOT NULL, station_id INTEGER NOT NULL, forecast_at TEXT NOT NULL, "
            "horizon_h INTEGER NOT NULL, predicted_load_kw REAL NOT NULL, predicted_busy_count INTEGER NOT NULL, "
            "predicted_idle_count INTEGER NOT NULL, congestion_level TEXT NOT NULL, is_peak INTEGER NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO chargers(id,status) VALUES(1,'idle'),(2,'reserved'),(3,'charging'),(4,'fault'),(5,'restarting')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO orders(id,status,amount_fen,ended_at) VALUES(1,'completed',1234,'2026-09-05T10:00:00+08:00')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO stations(id,name) VALUES(1,'一号站'),(2,'二号站')")));
    }

    ~ScopedDatabase()
    {
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_name);
    }

    QSqlDatabase database() const { return m_database; }

private:
    QString m_name;
    QSqlDatabase m_database;
};

} // namespace

class DashboardServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void summaryUsesFrozenAdminDashboardSchema()
    {
        ScopedDatabase db;
        DashboardService service(db.database());

        const QJsonObject summary = service.summary(7);

        QVERIFY(summary.value(QStringLiteral("revenue")).isObject());
        QVERIFY(summary.value(QStringLiteral("statusCounts")).isObject());
        QCOMPARE(summary.value(QStringLiteral("trend")).toArray().size(), 7);
        QVERIFY(summary.value(QStringLiteral("alerts")).isArray());

        const QJsonObject status = summary.value(QStringLiteral("statusCounts")).toObject();
        QCOMPARE(status.value(QStringLiteral("idle")).toInt(), 1);
        QCOMPARE(status.value(QStringLiteral("reserved")).toInt(), 1);
        QCOMPARE(status.value(QStringLiteral("charging")).toInt(), 1);
        QCOMPARE(status.value(QStringLiteral("fault")).toInt(), 1);
        QCOMPARE(status.value(QStringLiteral("restarting")).toInt(), 1);
        QCOMPARE(status.value(QStringLiteral("total")).toInt(), 5);
    }

    void revenueUsesShanghaiNaturalDayAndMonthAndCompletedOrdersOnly()
    {
        ScopedDatabase db;
        QSqlQuery query(db.database());
        QVERIFY(query.exec(QStringLiteral("DELETE FROM orders")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO orders(id,status,amount_fen,ended_at) VALUES"
            "(1,'completed',100,'2026-09-01T00:00:00+08:00'),"
            "(2,'completed',200,'2026-08-31T23:59:59+08:00'),"
            "(3,'completed',300,'2026-08-15T12:00:00+08:00'),"
            "(4,'charging',400,'2026-09-01T00:10:00+08:00')")));

        const QDateTime now(QDate(2026, 9, 1), QTime(0, 30), QTimeZone("Asia/Shanghai"));
        const QJsonObject summary = DashboardService(db.database()).summary(7, now);
        const QJsonObject revenue = summary.value(QStringLiteral("revenue")).toObject();

        QCOMPARE(revenue.value(QStringLiteral("todayRevenueFen")).toInteger(), 100);
        QCOMPARE(revenue.value(QStringLiteral("monthRevenueFen")).toInteger(), 100);
        QCOMPARE(revenue.value(QStringLiteral("totalRevenueFen")).toInteger(), 600);

        const QJsonArray trend = summary.value(QStringLiteral("trend")).toArray();
        QCOMPARE(trend.size(), 7);
        QCOMPARE(trend.at(5).toObject().value(QStringLiteral("date")).toString(), QStringLiteral("2026-08-31"));
        QCOMPARE(trend.at(5).toObject().value(QStringLiteral("revenueFen")).toInteger(), 200);
        QCOMPARE(trend.at(6).toObject().value(QStringLiteral("date")).toString(), QStringLiteral("2026-09-01"));
        QCOMPARE(trend.at(6).toObject().value(QStringLiteral("revenueFen")).toInteger(), 100);
    }

    void alertsUseActiveForecastAndContractFilterOrder()
    {
        ScopedDatabase db;
        QSqlQuery query(db.database());
        QVERIFY(query.exec(QStringLiteral("INSERT INTO forecast_runs(run_id,status) VALUES('active-run','active'),('old-run','superseded')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO forecasts(run_id,station_id,forecast_at,horizon_h,predicted_load_kw,predicted_busy_count,predicted_idle_count,congestion_level,is_peak) VALUES"
            "('active-run',2,'2026-09-01T02:00:00+08:00',2,80.0,8,2,'high',0),"
            "('active-run',1,'2026-09-01T02:00:00+08:00',2,60.0,6,4,'medium',1),"
            "('active-run',1,'2026-09-01T01:00:00+08:00',1,20.0,2,8,'low',0),"
            "('old-run',1,'2026-09-01T00:30:00+08:00',1,90.0,9,1,'high',1)")));

        const QDateTime now(QDate(2026, 9, 1), QTime(0, 30), QTimeZone("Asia/Shanghai"));
        const QJsonArray alerts = DashboardService(db.database()).summary(7, now)
                                      .value(QStringLiteral("alerts")).toArray();

        QCOMPARE(alerts.size(), 2);
        QCOMPARE(alerts.at(0).toObject().value(QStringLiteral("stationId")).toInt(), 1);
        QCOMPARE(alerts.at(0).toObject().value(QStringLiteral("stationName")).toString(), QStringLiteral("一号站"));
        QCOMPARE(alerts.at(0).toObject().value(QStringLiteral("congestionLevel")).toString(), QStringLiteral("medium"));
        QVERIFY(alerts.at(0).toObject().value(QStringLiteral("isPeak")).toBool());
        QCOMPARE(alerts.at(1).toObject().value(QStringLiteral("stationId")).toInt(), 2);
        QCOMPARE(alerts.at(1).toObject().value(QStringLiteral("congestionLevel")).toString(), QStringLiteral("high"));
    }
};

QTEST_MAIN(DashboardServiceTest)
#include "tst_dashboard_service.moc"
