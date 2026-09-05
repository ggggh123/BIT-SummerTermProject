#pragma once

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

class SnapshotWriter
{
public:
    explicit SnapshotWriter(QSqlDatabase database);

    bool write(const QString &path, QString *errorMessage = nullptr) const;

private:
    QJsonObject buildSnapshot() const;
    int scalarInt(const QString &sql, int fallback = 0) const;

    QSqlDatabase m_database;
};
