#pragma once

#include "core/Result.h"

#include <QSqlDatabase>
#include <QString>

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager &) = delete;
    DatabaseManager &operator=(const DatabaseManager &) = delete;
    Result open(const QString &databasePath = QString());
    QSqlDatabase database() const;
    QString databasePath() const;

private:
    Result migrate();
    Result seed();
    bool execSql(const QString &sql, QString *errorMessage = nullptr);

    QString m_connectionName;
    QString m_databasePath;
};
