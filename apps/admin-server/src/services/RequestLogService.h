#pragma once

#include "core/Result.h"
#include "protocol/Envelope.h"

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

class RequestLogService
{
public:
    explicit RequestLogService(QSqlDatabase database);

    Result ensureSchema() const;
    Result record(const QString &requestId, const QString &action, const ev::protocol::ResponseEnvelope &response) const;
    Result list(const QString &requestIdFilter, int limit, int offset, QJsonObject *responseData) const;

private:
    QSqlDatabase m_database;
};
