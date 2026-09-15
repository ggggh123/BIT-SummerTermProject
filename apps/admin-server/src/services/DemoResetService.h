#pragma once
#include "core/Result.h"
#include "protocol/Envelope.h"
#include <QSqlDatabase>
#include <functional>

// 只在 DatabaseWorker 上使用；receipt 独立于可复位的业务表。
class DemoResetService {
public:
    DemoResetService(QSqlDatabase database, QString goldenPath, QString goldenHash,
                     QString snapshotPath, std::function<void()> resetCommitted);
    Result ensureSchema() const;
    QByteArray execute(const ev::protocol::RequestEnvelope &request, const QString &actor) const;
    QByteArray rejectReservedId(const ev::protocol::RequestEnvelope &request, const QString &actor) const;
    void retrySnapshot() const;
private:
    Result restore(const QString &requestId, const QString &actor) const;
    bool snapshot(qint64 version) const;
    bool validGoldenFile() const;
    QSqlDatabase m_database;
    QString m_goldenPath, m_goldenHash, m_snapshotPath;
    std::function<void()> m_resetCommitted;
};
