#pragma once

#include "core/Result.h"

#include <QSet>
#include <QSqlDatabase>
#include <QString>

struct LoginResult : Result
{
    QString token;
};

class AuthService
{
public:
    explicit AuthService(QSqlDatabase database);

    LoginResult login(const QString &username, const QString &password) const;
    bool isTokenValid(const QString &token) const;

private:
    QString issueToken(const QString &username) const;

    QSqlDatabase m_database;
    mutable QSet<QString> m_adminTokens;
};

