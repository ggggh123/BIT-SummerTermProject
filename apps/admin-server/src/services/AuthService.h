#pragma once

#include "core/Result.h"

#include <QSet>
#include <QHash>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

struct LoginResult : Result
{
    QString token;
    QJsonObject data;
};

class AuthService
{
public:
    explicit AuthService(QSqlDatabase database);

    LoginResult login(const QString &username, const QString &password) const;
    LoginResult loginUser(const QString &mobile) const;
    bool isTokenValid(const QString &token) const;
    bool isUserTokenValid(const QString &token) const;
    bool isSimulatorTokenValid(const QString &token) const;
    bool isMlTokenValid(const QString &token) const;
    int userIdForToken(const QString &token) const;
    QString adminIdentityForToken(const QString &token) const;

private:
    QString issueToken(const QString &username) const;
    QJsonObject adminObject(const QString &username) const;
    QJsonObject userObject(int userId) const;

    QSqlDatabase m_database;
    mutable QSet<QString> m_adminTokens;
    mutable QHash<QString, QString> m_adminIdentities;
    mutable QHash<QString, int> m_userTokens;
};
