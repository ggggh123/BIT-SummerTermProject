#include "services/AuthService.h"
#include "services/UserService.h"

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
        : m_name(QStringLiteral("user-flow-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
        , m_database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name))
    {
        m_database.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_database.open());

        QSqlQuery query(m_database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "mobile TEXT NOT NULL UNIQUE,"
            "nickname TEXT NOT NULL,"
            "avatar_path TEXT NOT NULL DEFAULT '',"
            "balance_fen INTEGER NOT NULL,"
            "status TEXT NOT NULL,"
            "registered_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE stations ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "address TEXT NOT NULL,"
            "latitude REAL NOT NULL,"
            "longitude REAL NOT NULL,"
            "price_fen_per_kwh INTEGER NOT NULL,"
            "forecast_enabled INTEGER NOT NULL,"
            "created_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE chargers ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "station_id INTEGER NOT NULL,"
            "code TEXT NOT NULL UNIQUE,"
            "type TEXT NOT NULL,"
            "power_kw REAL NOT NULL,"
            "status TEXT NOT NULL,"
            "charge_count INTEGER NOT NULL DEFAULT 0,"
            "total_duration_sec INTEGER NOT NULL DEFAULT 0,"
            "updated_at TEXT NOT NULL)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE orders ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "user_id INTEGER NOT NULL,"
            "charger_id INTEGER NOT NULL,"
            "status TEXT NOT NULL,"
            "reserved_at TEXT NOT NULL,"
            "started_at TEXT,"
            "ended_at TEXT,"
            "energy_kwh REAL NOT NULL DEFAULT 0,"
            "amount_fen INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO stations(name,address,latitude,longitude,price_fen_per_kwh,forecast_enabled,created_at) "
            "VALUES('测试站','北京市朝阳区',39.9,116.4,100,1,'2026-09-01T00:00:00+08:00')")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO chargers(station_id,code,type,power_kw,status,charge_count,total_duration_sec,updated_at) "
            "VALUES(1,'1001','fast',60,'idle',0,0,'2026-09-01T00:00:00+08:00')")));
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

class UserFlowTest : public QObject
{
    Q_OBJECT

private slots:
    void userLoginCreatesTokenAndMainChargeFlowSettles()
    {
        ScopedDatabase db;
        AuthService auth(db.database());
        UserService users(db.database());

        const LoginResult login = auth.loginUser(QStringLiteral("13800138000"));
        QVERIFY(login.ok);
        QVERIFY(auth.isUserTokenValid(login.token));
        const int userId = auth.userIdForToken(login.token);
        QVERIFY(userId > 0);
        QCOMPARE(login.data.value(QStringLiteral("user")).toObject().value(QStringLiteral("mobile")).toString(),
                 QStringLiteral("13800138000"));

        QJsonObject rechargeData;
        QVERIFY(users.recharge(userId, QJsonObject{{QStringLiteral("amountFen"), 500}}, &rechargeData).ok);
        QCOMPARE(rechargeData.value(QStringLiteral("balanceFen")).toInt(), 500);

        QJsonObject stationData;
        QVERIFY(users.stationList(QJsonObject{{QStringLiteral("latitude"), 39.9}, {QStringLiteral("longitude"), 116.4}}, &stationData).ok);
        QCOMPARE(stationData.value(QStringLiteral("stations")).toArray().size(), 1);

        QJsonObject reserveData;
        QVERIFY(users.reserve(userId, QJsonObject{{QStringLiteral("chargerId"), 1}}, &reserveData).ok);
        const int orderId = reserveData.value(QStringLiteral("order")).toObject().value(QStringLiteral("orderId")).toInt();
        QVERIFY(orderId > 0);
        QCOMPARE(reserveData.value(QStringLiteral("order")).toObject().value(QStringLiteral("status")).toString(),
                 QStringLiteral("reserved"));

        QJsonObject startData;
        QVERIFY(users.start(userId, QJsonObject{{QStringLiteral("orderId"), orderId}}, &startData).ok);
        QCOMPARE(startData.value(QStringLiteral("order")).toObject().value(QStringLiteral("status")).toString(),
                 QStringLiteral("charging"));

        QJsonObject stopData;
        // 明确模拟已接收的 1 kWh/100 分遥测，停止本身不得凭空产生费用。
        QSqlQuery sample(db.database());
        QVERIFY(sample.exec(QStringLiteral("UPDATE orders SET energy_kwh=1, amount_fen=100")));
        QVERIFY(users.stop(userId, QJsonObject{{QStringLiteral("orderId"), orderId}}, &stopData).ok);
        QVERIFY(stopData.value(QStringLiteral("order")).toObject().value(QStringLiteral("endedAt")).isString());

        QJsonObject settleData;
        QVERIFY(users.settle(userId, QJsonObject{{QStringLiteral("orderId"), orderId}}, &settleData).ok);
        QCOMPARE(settleData.value(QStringLiteral("order")).toObject().value(QStringLiteral("status")).toString(),
                 QStringLiteral("completed"));
        QCOMPARE(settleData.value(QStringLiteral("balanceFen")).toInt(), 400);
    }
};

QTEST_MAIN(UserFlowTest)
#include "tst_user_flow.moc"
