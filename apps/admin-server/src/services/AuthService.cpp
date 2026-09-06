#include "services/AuthService.h"
#include "core/BusinessTime.h"
#include "db/SqlTransaction.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <utility>

namespace {

const QRegularExpression kMobilePattern(QStringLiteral("^1[3-9][0-9]{9}$"));

QString passwordHash(const QString &password)
{
    const QByteArray digest = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

} // namespace

AuthService::AuthService(QSqlDatabase database)
    : m_database(std::move(database))
{
}

LoginResult AuthService::login(const QString &username, const QString &password) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT password_hash FROM admins WHERE username = ?"));
    query.addBindValue(username.trimmed());
    if (!query.exec()) {
        return {false, QStringLiteral("DB_ERROR"), query.lastError().text(), QString()};
    }

    if (!query.next() || query.value(0).toString() != passwordHash(password)) {
        return {false, QStringLiteral("INVALID_CREDENTIALS"), QStringLiteral("账号或密码错误"), QString()};
    }

    const QString token = issueToken(username.trimmed());
    m_adminTokens.insert(token);
    m_adminIdentities.insert(token, username.trimmed());
    return {true, QStringLiteral("OK"), QStringLiteral("login success"), token,
            QJsonObject{{QStringLiteral("token"), token}, {QStringLiteral("admin"), adminObject(username.trimmed())}}};
}

LoginResult AuthService::loginUser(const QString &mobile) const
{
    const QString normalized = mobile.trimmed();
    if (!kMobilePattern.match(normalized).hasMatch()) {
        return {false, QStringLiteral("INVALID_PHONE"), QStringLiteral("手机号格式无效"), QString(), {}};
    }

    SqlTransaction transaction(m_database);
    if (!transaction.transaction()) {
        const Result error = databaseFailure(transaction.lastError());
        return {false, error.code, error.message, QString(), {}};
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM users WHERE mobile = ?"));
    query.addBindValue(normalized);
    if (!query.exec()) {
        return {false, QStringLiteral("DB_ERROR"), query.lastError().text(), QString(), {}};
    }

    int userId = 0;
    if (query.next()) {
        userId = query.value(0).toInt();
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO users(mobile, nickname, avatar_path, balance_fen, status, registered_at) "
            "VALUES(?, ?, '', 0, 'active', ?)"));
        query.addBindValue(normalized);
        query.addBindValue(QStringLiteral("用户") + normalized.right(4));
        query.addBindValue(BusinessTime::now());
        if (!query.exec()) {
            return {false, QStringLiteral("DB_ERROR"), query.lastError().text(), QString(), {}};
        }
        userId = query.lastInsertId().toInt();
    }

    query.finish();
    if (!transaction.commit()) {
        const Result error = databaseFailure(transaction.lastError());
        return {false, error.code, error.message, QString(), {}};
    }
    const QString token = issueToken(normalized);
    m_userTokens.insert(token, userId);
    return {true, QStringLiteral("OK"), QStringLiteral("login success"), token,
            QJsonObject{{QStringLiteral("token"), token}, {QStringLiteral("user"), userObject(userId)}}};
}

bool AuthService::isTokenValid(const QString &token) const
{
    return !token.trimmed().isEmpty() && m_adminTokens.contains(token.trimmed());
}

bool AuthService::isUserTokenValid(const QString &token) const
{
    return !token.trimmed().isEmpty() && m_userTokens.contains(token.trimmed());
}

bool AuthService::isSimulatorTokenValid(const QString &token) const
{
    const QString normalized = token.trimmed();
    return normalized == QStringLiteral("sim-token")
        || normalized == QStringLiteral("simulator-token")
        || normalized == QStringLiteral("demo-simulator-token");
}

bool AuthService::isMlTokenValid(const QString &token) const
{
    const QString normalized = token.trimmed();
    return normalized == QStringLiteral("ml-token")
        || normalized == QStringLiteral("forecast-token")
        || normalized == QStringLiteral("demo-ml-token");
}

int AuthService::userIdForToken(const QString &token) const
{
    return m_userTokens.value(token.trimmed(), 0);
}

QString AuthService::adminIdentityForToken(const QString &token) const
{
    return m_adminIdentities.value(token.trimmed());
}

QString AuthService::issueToken(const QString &username) const
{
    const QString raw = QStringLiteral("%1|%2|%3")
        .arg(username,
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             QUuid::createUuid().toString(QUuid::WithoutBraces));
    return QString::fromLatin1(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QJsonObject AuthService::adminObject(const QString &username) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id, username, created_at FROM admins WHERE username = ?"));
    query.addBindValue(username);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return {
        {QStringLiteral("adminId"), query.value(0).toInt()},
        {QStringLiteral("username"), query.value(1).toString()},
        {QStringLiteral("createdAt"), query.value(2).toString()}
    };
}

QJsonObject AuthService::userObject(int userId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, mobile, nickname, avatar_path, balance_fen, status, registered_at "
        "FROM users WHERE id = ?"));
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return {
        {QStringLiteral("userId"), query.value(0).toInt()},
        {QStringLiteral("mobile"), query.value(1).toString()},
        {QStringLiteral("nickname"), query.value(2).toString()},
        {QStringLiteral("avatarPath"), query.value(3).toString()},
        {QStringLiteral("balanceFen"), query.value(4).toLongLong()},
        {QStringLiteral("status"), query.value(5).toString()},
        {QStringLiteral("registeredAt"), query.value(6).toString()}
    };
}
