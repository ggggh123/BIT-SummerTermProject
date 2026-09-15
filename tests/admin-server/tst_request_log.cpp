#include "services/RequestLogService.h"

#include "protocol/JsonEnvelope.h"

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
        : m_name(QStringLiteral("request-log-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
        , m_database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name))
    {
        m_database.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_database.open());

        QSqlQuery query(m_database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE request_log ("
            "request_id TEXT PRIMARY KEY,"
            "action TEXT NOT NULL,"
            "code TEXT NOT NULL,"
            "response_json TEXT NOT NULL,"
            "created_at TEXT NOT NULL)")));
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

class RequestLogTest : public QObject
{
    Q_OBJECT

private slots:
    void recordsActionCodeAndResponseWithoutToken()
    {
        ScopedDatabase db;
        RequestLogService service(db.database());
        const ev::protocol::ResponseEnvelope response{
            QStringLiteral("req-1"),
            true,
            QStringLiteral("OK"),
            QStringLiteral("success"),
            QJsonObject{{QStringLiteral("token"), QStringLiteral("secret-token")}}
        };

        const Result result = service.record(
            QStringLiteral("req-1"),
            QStringLiteral("admin.login"),
            response);

        QVERIFY(result.ok);

        QSqlQuery query(db.database());
        QVERIFY(query.exec(QStringLiteral("SELECT request_id, action, code, response_json FROM request_log")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("req-1"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("admin.login"));
        QCOMPARE(query.value(2).toString(), QStringLiteral("OK"));
        QVERIFY(!query.value(3).toString().contains(QStringLiteral("secret-token")));
    }

    void listsNewestFirstAndFiltersByRequestId()
    {
        ScopedDatabase db;
        RequestLogService service(db.database());

        QVERIFY(service.record(QStringLiteral("req-a"), QStringLiteral("system.health"),
                               {QStringLiteral("req-a"), true, QStringLiteral("OK"), QStringLiteral("ready"), QJsonObject{}}).ok);
        QVERIFY(service.record(QStringLiteral("req-b"), QStringLiteral("admin.dashboard"),
                               {QStringLiteral("req-b"), false, QStringLiteral("UNAUTHORIZED"), QStringLiteral("denied"), QJsonObject{}}).ok);

        QJsonObject data;
        const Result result = service.list(QStringLiteral("req-b"), 10, 0, &data);

        QVERIFY(result.ok);
        QCOMPARE(data.value(QStringLiteral("total")).toInt(), 1);
        const QJsonArray rows = data.value(QStringLiteral("items")).toArray();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toObject().value(QStringLiteral("requestId")).toString(), QStringLiteral("req-b"));
        QCOMPARE(rows.first().toObject().value(QStringLiteral("action")).toString(), QStringLiteral("admin.dashboard"));
        QCOMPARE(rows.first().toObject().value(QStringLiteral("code")).toString(), QStringLiteral("UNAUTHORIZED"));
    }
};

QTEST_MAIN(RequestLogTest)
#include "tst_request_log.moc"
