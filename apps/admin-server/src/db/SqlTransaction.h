#pragma once

#include "core/Result.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

inline Result databaseFailure(const QSqlError &error)
{
    const int nativeCode = error.nativeErrorCode().toInt() & 0xff;
    return nativeCode == 5 || nativeCode == 6
        ? Result::failure(QStringLiteral("DB_BUSY"), QStringLiteral("数据库暂时忙，请重试"))
        : Result::failure(QStringLiteral("INTERNAL_ERROR"), QStringLiteral("数据库操作失败"));
}

// 服务直接调用拥有自己的事务；TCP 外层事务中使用 SAVEPOINT，避免嵌套 BEGIN。
class SqlTransaction
{
public:
    explicit SqlTransaction(QSqlDatabase database, bool outer = false)
        : m_database(database), m_outer(outer),
          m_name(QStringLiteral("sp_") + QUuid::createUuid().toString(QUuid::Id128)) {}
    ~SqlTransaction() { rollback(); }
    SqlTransaction(const SqlTransaction &) = delete;
    SqlTransaction &operator=(const SqlTransaction &) = delete;
    operator QSqlDatabase() const { return m_database; }
    QSqlError lastError() const { return m_error; }
    bool transaction()
    {
        if (m_active) return false;
        m_active = execute(m_outer ? QStringLiteral("BEGIN IMMEDIATE") : QStringLiteral("SAVEPOINT ") + m_name);
        return m_active;
    }
    bool commit()
    {
        if (!m_active || !execute(m_outer ? QStringLiteral("COMMIT") : QStringLiteral("RELEASE SAVEPOINT ") + m_name)) return false;
        m_active = false;
        return true;
    }
    void rollback()
    {
        if (!m_active) return;
        QSqlQuery query(m_database);
        if (m_outer) query.exec(QStringLiteral("ROLLBACK"));
        else {
            query.exec(QStringLiteral("ROLLBACK TO SAVEPOINT ") + m_name);
            query.exec(QStringLiteral("RELEASE SAVEPOINT ") + m_name);
        }
        m_active = false;
    }
private:
    bool execute(const QString &sql)
    {
        QSqlQuery query(m_database);
        if (query.exec(sql)) return true;
        m_error = query.lastError();
        return false;
    }
    QSqlDatabase m_database;
    bool m_outer;
    QString m_name;
    QSqlError m_error;
    bool m_active = false;
};
