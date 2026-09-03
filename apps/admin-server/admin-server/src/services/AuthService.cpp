#include "services/AuthService.h"

#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
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

Result AuthService::login(const QString &username, const QString &password) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT password_hash FROM admins WHERE username = ?"));
    query.addBindValue(username.trimmed());
    if (!query.exec()) {
        return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
    }

    if (!query.next() || query.value(0).toString() != passwordHash(password)) {
        return Result::failure(QStringLiteral("UNAUTHORIZED"), QStringLiteral("账号或密码错误"));
    }

    return Result::success(QStringLiteral("login success"));
}

