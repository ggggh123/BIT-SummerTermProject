#include "services/AdminService.h"

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
        : m_name(QStringLiteral("admin-service-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
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
            "CREATE TABLE events (id INTEGER PRIMARY KEY AUTOINCREMENT, event_type TEXT NOT NULL, "
            "entity_type TEXT NOT NULL, entity_id INTEGER, message TEXT NOT NULL, created_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("CREATE TABLE snapshot_meta (id INTEGER PRIMARY KEY, version INTEGER NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO snapshot_meta(id, version) VALUES(1, 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO users(mobile,nickname,avatar_path,balance_fen,status,registered_at) "
            "VALUES('13800138000','用户8000','',500,'active','2026-09-01T00:00:00+08:00')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO stations(name,address,latitude,longitude,price_fen_per_kwh,forecast_enabled,created_at) "
            "VALUES('测试站','北京',39.9,116.4,100,1,'2026-09-01T00:00:00+08:00')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO chargers(station_id,code,type,power_kw,status,charge_count,total_duration_sec,updated_at) "
            "VALUES(1,'1001','fast',60,'fault',0,0,'2026-09-01T00:00:00+08:00')")));
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

class AdminServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void stationCreateBuildsStationAndIdleChargers()
    {
        ScopedDatabase db;
        AdminService service(db.database());

        QJsonObject data;
        const Result result = service.stationCreate(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("新站")},
            {QStringLiteral("address"), QStringLiteral("北京市海淀区")},
            {QStringLiteral("latitude"), 39.98},
            {QStringLiteral("longitude"), 116.31},
            {QStringLiteral("priceFenPerKwh"), 120},
            {QStringLiteral("fastChargerCount"), 1},
            {QStringLiteral("slowChargerCount"), 2}}, &data);

        QVERIFY(result.ok);
        QCOMPARE(data.value(QStringLiteral("station")).toObject().value(QStringLiteral("forecastEnabled")).toBool(), false);
        QCOMPARE(data.value(QStringLiteral("chargers")).toArray().size(), 3);
        QCOMPARE(data.value(QStringLiteral("chargers")).toArray().at(0).toObject().value(QStringLiteral("status")).toString(),
                 QStringLiteral("idle"));
    }

    void chargerRestartRequiresFaultAndRecordsEvent()
    {
        ScopedDatabase db;
        AdminService service(db.database());

        QJsonObject data;
        QVERIFY(service.chargerRestart(QJsonObject{{QStringLiteral("chargerId"), 1}}, &data).ok);
        QCOMPARE(data.value(QStringLiteral("charger")).toObject().value(QStringLiteral("status")).toString(),
                 QStringLiteral("restarting"));

        QSqlQuery query(db.database());
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM events WHERE event_type='admin.charger_restart'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);

        QVERIFY(service.finishRestart(1).ok);
        QVERIFY(query.exec(QStringLiteral("SELECT status FROM chargers WHERE id=1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("idle"));

        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM events WHERE event_type LIKE 'admin.charger_restart%'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 2);
    }

    void userListAndSetStatusFollowContract()
    {
        ScopedDatabase db;
        AdminService service(db.database());

        QJsonObject listData;
        QVERIFY(service.userList(QJsonObject{{QStringLiteral("mobileLike"), QStringLiteral("13800138")}}, &listData).ok);
        QCOMPARE(listData.value(QStringLiteral("total")).toInt(), 1);
        QCOMPARE(listData.value(QStringLiteral("items")).toArray().size(), 1);

        QJsonObject statusData;
        QVERIFY(service.userSetStatus(QJsonObject{{QStringLiteral("userId"), 1}, {QStringLiteral("status"), QStringLiteral("frozen")}}, &statusData).ok);
        QCOMPARE(statusData.value(QStringLiteral("user")).toObject().value(QStringLiteral("status")).toString(),
                 QStringLiteral("frozen"));
    }

    void restartCompletionRollsBackWhenEventInsertFails()
    {
        ScopedDatabase db;
        AdminService service(db.database());
        QSqlQuery query(db.database());
        QVERIFY(query.exec(QStringLiteral("UPDATE chargers SET status='restarting' WHERE id=1")));
        QVERIFY(query.exec(QStringLiteral("CREATE TRIGGER fail_event BEFORE INSERT ON events BEGIN SELECT RAISE(ABORT, 'private failure'); END")));
        const Result result = service.finishRestart(1);
        QVERIFY(!result.ok);
        QCOMPARE(result.code, QStringLiteral("INTERNAL_ERROR"));
        QVERIFY(query.exec(QStringLiteral("SELECT status FROM chargers WHERE id=1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("restarting"));
        QVERIFY(query.exec(QStringLiteral("SELECT version FROM snapshot_meta WHERE id=1")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
    }
};

QTEST_MAIN(AdminServiceTest)
#include "tst_admin_service.moc"
