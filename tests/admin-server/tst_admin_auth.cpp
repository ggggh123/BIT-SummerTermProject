#include "services/AuthService.h"

#include <QCryptographicHash>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QUuid>

namespace {

QString hashPassword(const QString &password)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
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
};

QTEST_MAIN(AdminAuthTest)
#include "tst_admin_auth.moc"
