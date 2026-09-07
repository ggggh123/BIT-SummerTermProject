#include "services/AuthService.h"
#include "services/RequestPreflight.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QUuid>

namespace {

class ScopedEnvironmentVariable
{
public:
    explicit ScopedEnvironmentVariable(const QByteArray &name)
        : m_name(name)
        , m_wasSet(qEnvironmentVariableIsSet(name.constData()))
        , m_value(qgetenv(name.constData()))
    {
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_wasSet) qputenv(m_name.constData(), m_value);
        else qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet;
    QByteArray m_value;
};

QString hashPassword(const QString &password)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
}

Result simulatorStatusPreflight(const AuthService &service, const QString &token)
{
    const ev::protocol::RequestEnvelope request{
        1,
        QStringLiteral("simulator-auth-test"),
        ev::actions::SimulatorStatus,
        token,
        QJsonObject{{QStringLiteral("simulatedAt"), QStringLiteral("2026-09-07T00:00:00+08:00")},
                    {QStringLiteral("eventCount"), 0},
                    {QStringLiteral("state"), QStringLiteral("running")}}};
    const QString role = service.isSimulatorTokenValid(token) ? QStringLiteral("simulator") : QString();
    return RequestPreflight::check(role, request);
}

class ScopedDatabase
{
public:
    ScopedDatabase()
        : m_name(QStringLiteral("auth-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
        , m_database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name))
    {
        m_database.setDatabaseName(QStringLiteral(":memory:"));
        QVERIFY(m_database.open());

        QSqlQuery query(m_database);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE admins ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "username TEXT NOT NULL UNIQUE,"
            "password_hash TEXT NOT NULL,"
            "created_at TEXT NOT NULL)")));
        query.prepare(QStringLiteral(
            "INSERT INTO admins(username, password_hash, created_at) VALUES(?, ?, ?)"));
        query.addBindValue(QStringLiteral("admin"));
        query.addBindValue(hashPassword(QStringLiteral("123456")));
        query.addBindValue(QStringLiteral("2026-09-01T00:00:00+08:00"));
        QVERIFY(query.exec());
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

class AdminAuthTest : public QObject
{
    Q_OBJECT

private slots:
    void validCredentialsIssueReusableToken()
    {
        ScopedDatabase db;
        AuthService service(db.database());

        const LoginResult result = service.login(QStringLiteral("admin"), QStringLiteral("123456"));

        QVERIFY(result.ok);
        QCOMPARE(result.code, QStringLiteral("OK"));
        QVERIFY(!result.token.isEmpty());
        QVERIFY(service.isTokenValid(result.token));
        QCOMPARE(result.data.value(QStringLiteral("token")).toString(), result.token);
        QCOMPARE(result.data.value(QStringLiteral("admin")).toObject().value(QStringLiteral("adminId")).toInt(), 1);
        QCOMPARE(result.data.value(QStringLiteral("admin")).toObject().value(QStringLiteral("username")).toString(),
                 QStringLiteral("admin"));
    }

    void invalidCredentialsUseContractFailureCode()
    {
        ScopedDatabase db;
        AuthService service(db.database());

        const LoginResult result = service.login(QStringLiteral("admin"), QStringLiteral("bad-password"));

        QVERIFY(!result.ok);
        QCOMPARE(result.code, QStringLiteral("INVALID_CREDENTIALS"));
        QVERIFY(result.token.isEmpty());
    }

    void configuredSimulatorTokenAuthorizesStatusPreflight()
    {
        ScopedEnvironmentVariable environment(QByteArrayLiteral("EV_SIMULATOR_TOKEN"));
        const QByteArray configuredToken = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
        QVERIFY(qputenv("EV_SIMULATOR_TOKEN", configuredToken));
        AuthService service{QSqlDatabase()};

        const Result result = simulatorStatusPreflight(service, QString::fromUtf8(configuredToken));

        QVERIFY(result.ok);
        QCOMPARE(result.code, QStringLiteral("OK"));
    }

    void configuredSimulatorTokenRejectsEveryOtherToken()
    {
        ScopedEnvironmentVariable environment(QByteArrayLiteral("EV_SIMULATOR_TOKEN"));
        const QByteArray configuredToken = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
        QVERIFY(qputenv("EV_SIMULATOR_TOKEN", configuredToken));
        AuthService service{QSqlDatabase()};

        const Result randomResult = simulatorStatusPreflight(
            service, QUuid::createUuid().toString(QUuid::WithoutBraces));
        const Result paddedResult = simulatorStatusPreflight(
            service, QStringLiteral(" ") + QString::fromUtf8(configuredToken));
        const Result legacyResult = simulatorStatusPreflight(service, QStringLiteral("demo-simulator-token"));

        QVERIFY(!randomResult.ok);
        QCOMPARE(randomResult.code, QStringLiteral("AUTH_REQUIRED"));
        QVERIFY(!paddedResult.ok);
        QCOMPARE(paddedResult.code, QStringLiteral("AUTH_REQUIRED"));
        QVERIFY(!legacyResult.ok);
        QCOMPARE(legacyResult.code, QStringLiteral("AUTH_REQUIRED"));
        QVERIFY(service.isMlTokenValid(QStringLiteral("demo-ml-token")));
    }

    void missingOrEmptySimulatorConfigurationPreservesLegacyTokens()
    {
        ScopedEnvironmentVariable environment(QByteArrayLiteral("EV_SIMULATOR_TOKEN"));
        AuthService service{QSqlDatabase()};

        QVERIFY(qunsetenv("EV_SIMULATOR_TOKEN"));
        QVERIFY(simulatorStatusPreflight(service, QStringLiteral("sim-token")).ok);
        QVERIFY(simulatorStatusPreflight(service, QStringLiteral("simulator-token")).ok);
        QVERIFY(simulatorStatusPreflight(service, QStringLiteral("demo-simulator-token")).ok);

        QVERIFY(qputenv("EV_SIMULATOR_TOKEN", QByteArray()));
        QVERIFY(simulatorStatusPreflight(service, QStringLiteral("sim-token")).ok);
        QVERIFY(simulatorStatusPreflight(service, QStringLiteral("simulator-token")).ok);
        QVERIFY(simulatorStatusPreflight(service, QStringLiteral("demo-simulator-token")).ok);
    }
};

QTEST_MAIN(AdminAuthTest)
#include "tst_admin_auth.moc"
