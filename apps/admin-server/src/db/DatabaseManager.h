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
    // 只解析路径，不创建目录或打开文件；供启动前文件身份校验复用。
    static QString resolvePath(const QString &databasePath = QString());
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
