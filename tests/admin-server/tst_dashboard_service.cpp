#include "services/DashboardService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
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
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE orders (id INTEGER PRIMARY KEY, status TEXT NOT NULL, amount_fen INTEGER NOT NULL, ended_at TEXT)")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE forecast_runs (run_id TEXT PRIMARY KEY, status TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE forecasts (run_id TEXT NOT NULL, station_id INTEGER NOT NULL, forecast_at TEXT NOT NULL, "
            "horizon_h INTEGER NOT NULL, predicted_load_kw REAL NOT NULL, predicted_busy_count INTEGER NOT NULL, "
            "predicted_idle_count INTEGER NOT NULL, congestion_level TEXT NOT NULL, is_peak INTEGER NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO chargers(id,status) VALUES(1,'idle'),(2,'reserved'),(3,'charging'),(4,'fault'),(5,'restarting')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO orders(id,status,amount_fen,ended_at) VALUES(1,'completed',1234,'2026-09-05T10:00:00+08:00')")));
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
};

QTEST_MAIN(DashboardServiceTest)
#include "tst_dashboard_service.moc"
