#include "services/AuthService.h"
#include "services/UserService.h"

#include <QJsonArray>
#include <QDateTime>
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
    void statisticsAggregateAllOwnedOrdersAndCalendarDays()
    {
        ScopedDatabase db;
        AuthService auth(db.database());
        UserService users(db.database());
        const auto login = auth.loginUser(QStringLiteral("13800138000"));
        QVERIFY(login.ok);
        const int uid = auth.userIdForToken(login.token);
        const QDate today = QDateTime::currentDateTimeUtc().toOffsetFromUtc(8 * 3600).date();
        const QDate yesterday = today.addDays(-1);
        const QDate previousMonth = QDate(today.year(), today.month(), 1).addDays(-1);
        QSqlQuery q(db.database());
        const auto insert = [&](int user, const QString &status, const QDate &date,
                                double energy, int amount, bool started) {
            q.prepare("INSERT INTO orders(user_id,charger_id,status,reserved_at,started_at,ended_at,energy_kwh,amount_fen) VALUES(?,1,?,?,?,?,?,?)");
            q.addBindValue(user); q.addBindValue(status);
            q.addBindValue(date.toString(Qt::ISODate) + "T08:00:00+08:00");
            q.addBindValue(started ? QVariant(date.toString(Qt::ISODate) + "T08:00:00+08:00") : QVariant());
            q.addBindValue(date.toString(Qt::ISODate) + "T09:00:00+08:00");
            q.addBindValue(energy); q.addBindValue(amount);
            return q.exec();
        };
        QVERIFY(insert(uid, "completed", previousMonth, 3.0, 300, true));
        QVERIFY(insert(uid, "completed", yesterday, 4.0, 400, true));
        QVERIFY(insert(uid, "charging", today, 2.0, 200, true));
        QVERIFY(insert(uid, "cancelled", today, 0, 0, false));
        QVERIFY(insert(uid, "reserved", today, 0, 0, false));
        QVERIFY(insert(uid + 1, "completed", today, 999, 99900, true));
        QJsonObject data;
        QVERIFY(users.usageStatistics(uid, &data).ok);
        QCOMPARE(data.value("userId").toInt(), uid);
        QCOMPARE(data.value("orderCount").toInt(), 3);
        QCOMPARE(data.value("completedCount").toInt(), 2);
        QCOMPARE(data.value("energyKwh").toDouble(), 9.0);
        QCOMPARE(data.value("paidFen").toInt(), 700);
        QCOMPARE(data.value("durationSec").toInt(), 10800);
        QCOMPARE(data.value("pendingSettlementCount").toInt(), 1);
        QCOMPARE(data.value("pendingSettlementFen").toInt(), 200);
        QCOMPARE(data.value("monthEnergyKwh").toDouble(), yesterday.month() == today.month() ? 6.0 : 2.0);
        const auto days = data.value("days").toArray();
        QCOMPARE(days.size(), 7);
        for (int i = 0; i < 7; ++i) {
            const auto date = today.addDays(i - 6);
            const double expected = (date == today ? 2.0 : 0) + (date == yesterday ? 4.0 : 0)
                + (date == previousMonth ? 3.0 : 0);
            QCOMPARE(days[i].toObject().value("date").toString(), date.toString(Qt::ISODate));
            QCOMPARE(days[i].toObject().value("energyKwh").toDouble(), expected);
        }
        QVERIFY(users.usageStatistics(uid + 2, &data).ok);
        QCOMPARE(data.value("energyKwh").toDouble(), 0.0);
        QCOMPARE(users.usageStatistics(0, &data).code, QStringLiteral("AUTH_REQUIRED"));
    }

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
