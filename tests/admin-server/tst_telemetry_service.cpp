#include "services/TelemetryService.h"

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
        : m_name(QStringLiteral("telemetry-service-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
        , m_database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name))
    {
        m_database.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_database.open());

        QSqlQuery query(m_database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, mobile TEXT NOT NULL UNIQUE, "
            "nickname TEXT NOT NULL, avatar_path TEXT NOT NULL DEFAULT '', balance_fen INTEGER NOT NULL, "
            "status TEXT NOT NULL, registered_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE stations (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, address TEXT NOT NULL, "
            "latitude REAL NOT NULL, longitude REAL NOT NULL, price_fen_per_kwh INTEGER NOT NULL, "
            "forecast_enabled INTEGER NOT NULL, created_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE chargers (id INTEGER PRIMARY KEY AUTOINCREMENT, station_id INTEGER NOT NULL, "
            "code TEXT NOT NULL UNIQUE, type TEXT NOT NULL, power_kw REAL NOT NULL, status TEXT NOT NULL, "
            "charge_count INTEGER NOT NULL DEFAULT 0, total_duration_sec INTEGER NOT NULL DEFAULT 0, updated_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE orders (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL, charger_id INTEGER NOT NULL, "
            "status TEXT NOT NULL, reserved_at TEXT NOT NULL, started_at TEXT, ended_at TEXT, "
            "energy_kwh REAL NOT NULL DEFAULT 0, amount_fen INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE telemetry (id INTEGER PRIMARY KEY AUTOINCREMENT, charger_id INTEGER NOT NULL, "
            "recorded_at TEXT NOT NULL, power_kw REAL NOT NULL, energy_increment_kwh REAL NOT NULL, event_type TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE events (id INTEGER PRIMARY KEY AUTOINCREMENT, event_type TEXT NOT NULL, "
            "entity_type TEXT NOT NULL, entity_id INTEGER, message TEXT NOT NULL, created_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO users(mobile,nickname,avatar_path,balance_fen,status,registered_at) VALUES('13800138000','用户8000','',500,'active','2026-09-01T00:00:00+08:00')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO stations(name,address,latitude,longitude,price_fen_per_kwh,forecast_enabled,created_at) VALUES('测试站','北京',39.9,116.4,100,1,'2026-09-01T00:00:00+08:00')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO chargers(station_id,code,type,power_kw,status,charge_count,total_duration_sec,updated_at) VALUES(1,'1001','fast',60,'charging',0,0,'2026-09-01T00:00:00+08:00')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO orders(user_id,charger_id,status,reserved_at,started_at,energy_kwh,amount_fen) VALUES(1,1,'charging','2026-09-01T00:00:00+08:00','2026-09-01T00:01:00+08:00',0,0)")));
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

class TelemetryServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void telemetryPushAccumulatesActiveOrderAmount()
    {
        ScopedDatabase db;
        TelemetryService service(db.database());

        QJsonObject data;
        const Result result = service.telemetryPush(QJsonObject{
            {QStringLiteral("chargerId"), 1},
            {QStringLiteral("recordedAt"), QStringLiteral("2026-09-01T00:02:00+08:00")},
            {QStringLiteral("powerKw"), 60.0},
            {QStringLiteral("energyIncrementKwh"), 1.25},
            {QStringLiteral("status"), QStringLiteral("charging")}}, &data);

        QVERIFY(result.ok);
        QCOMPARE(data.value(QStringLiteral("order")).toObject().value(QStringLiteral("energyKwh")).toDouble(), 1.25);
        QCOMPARE(data.value(QStringLiteral("order")).toObject().value(QStringLiteral("amountFen")).toInt(), 125);
    }

    void faultSetStopsChargingWithoutCompletingOrder()
    {
        ScopedDatabase db;
        TelemetryService service(db.database());

        QJsonObject data;
        QVERIFY(service.faultSet(QJsonObject{
            {QStringLiteral("chargerId"), 1},
            {QStringLiteral("fault"), true},
            {QStringLiteral("recordedAt"), QStringLiteral("2026-09-01T00:03:00+08:00")}}, &data).ok);

        QCOMPARE(data.value(QStringLiteral("charger")).toObject().value(QStringLiteral("status")).toString(),
                 QStringLiteral("fault"));

        QSqlQuery query(db.database());
        QVERIFY(query.exec(QStringLiteral("SELECT status, ended_at FROM orders WHERE id=1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("charging"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("2026-09-01T00:03:00+08:00"));
    }

    void simulatorStatusReturnsAuthoritativeChargers()
    {
        ScopedDatabase db;
        TelemetryService service(db.database());

        QJsonObject data;
        QVERIFY(service.simulatorStatus(QJsonObject{
            {QStringLiteral("state"), QStringLiteral("running")},
            {QStringLiteral("simulatedAt"), QStringLiteral("2026-09-01T00:04:00+08:00")},
            {QStringLiteral("eventCount"), 2}}, &data).ok);

        QVERIFY(data.value(QStringLiteral("acceptedAt")).isString());
        QCOMPARE(data.value(QStringLiteral("chargers")).toArray().size(), 1);
    }
};

QTEST_MAIN(TelemetryServiceTest)
#include "tst_telemetry_service.moc"
