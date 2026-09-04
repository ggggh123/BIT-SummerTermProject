#include "services/AuthService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <utility>

namespace {

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
    return {true, QStringLiteral("OK"), QStringLiteral("login success"), token};
}

bool AuthService::isTokenValid(const QString &token) const
{
    return !token.trimmed().isEmpty() && m_adminTokens.contains(token.trimmed());
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

