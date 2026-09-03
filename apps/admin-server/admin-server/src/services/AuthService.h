#pragma once

#include "core/Result.h"

#include <QSqlDatabase>
#include <QString>

class AuthService
{
public:
    explicit AuthService(QSqlDatabase database);

    Result login(const QString &username, const QString &password) const;

private:
    QSqlDatabase m_database;
};

