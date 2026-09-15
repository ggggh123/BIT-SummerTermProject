#pragma once

#include "core/Result.h"

#include <QSqlDatabase>
#include <QString>

// SQLite 连接的生命周期管理者：打开库、设 PRAGMA、执行 schema、种子数据；
// 服务端是数据库唯一 writer，本类即入口。
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
