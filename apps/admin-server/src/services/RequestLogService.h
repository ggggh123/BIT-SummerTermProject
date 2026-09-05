#pragma once

#include "core/Result.h"
#include "protocol/Envelope.h"

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <functional>

class RequestLogService
{
public:
    explicit RequestLogService(QSqlDatabase database);

    Result ensureSchema() const;
    Result record(const QString &requestId, const QString &action, const ev::protocol::ResponseEnvelope &response) const;
    Result list(const QString &requestIdFilter, int limit, int offset, QJsonObject *responseData) const;
    QByteArray execute(const ev::protocol::RequestEnvelope &request, const QString &actor,
                       const std::function<ev::protocol::ResponseEnvelope()> &business,
                       bool twoPhase = false) const;

private:
    QSqlDatabase m_database;
};
