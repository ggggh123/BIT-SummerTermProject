#pragma once

#include "core/Result.h"
#include "services/TokenRoles.h"

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

// 登录与鉴权：签发/校验 admin 和 user 的内存会话 token；模拟器/ML 走配置的固定 token。
class AuthService
{
public:
    explicit AuthService(QSqlDatabase database);
    AuthService(QSqlDatabase database, TokenRoles tokenRoles);

    LoginResult login(const QString &username, const QString &password) const;
    LoginResult loginUser(const QString &mobile) const;
    bool isTokenValid(const QString &token) const;
    bool isUserTokenValid(const QString &token) const;
    bool isSimulatorTokenValid(const QString &token) const;
    bool isMlTokenValid(const QString &token) const;
    int userIdForToken(const QString &token) const;
    QString adminIdentityForToken(const QString &token) const;
    TokenRoles tokenRoles() const;

private:
    QString issueToken(const QString &username) const;
    QJsonObject adminObject(const QString &username) const;
    QJsonObject userObject(int userId) const;

    QSqlDatabase m_database;
    TokenRoles m_tokenRoles;
    mutable QSet<QString> m_adminTokens;
    mutable QHash<QString, QString> m_adminIdentities;
    mutable QHash<QString, int> m_userTokens;
};
