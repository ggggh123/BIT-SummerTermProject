#pragma once

#include "core/Result.h"

#include <QSqlDatabase>
#include <QString>

class DatabaseManager
{
public:
    Result open();
    QSqlDatabase database() const;
    QString databasePath() const;

private:
    Result migrate();
    Result seed();
    bool execSql(const QString &sql, QString *errorMessage = nullptr);

    QString m_connectionName = QStringLiteral("management");
    QString m_databasePath;
};

