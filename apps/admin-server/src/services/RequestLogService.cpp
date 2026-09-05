#include "services/RequestLogService.h"

#include "protocol/JsonEnvelope.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>

namespace {

QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

QJsonValue withoutTokenFields(const QJsonValue &value)
{
    if (value.isArray()) {
        QJsonArray cleanArray;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            cleanArray.append(withoutTokenFields(item));
        }
        return cleanArray;
    }

    if (!value.isObject()) {
        return value;
    }

    QJsonObject cleanObject;
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.key().compare(QStringLiteral("token"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        cleanObject.insert(it.key(), withoutTokenFields(it.value()));
    }
    return cleanObject;
}

QByteArray sanitizedResponseJson(const ev::protocol::ResponseEnvelope &response)
{
    ev::protocol::ResponseEnvelope cleanResponse = response;
    cleanResponse.data = withoutTokenFields(response.data);
    return ev::protocol::toJson(cleanResponse);
}

bool columnExists(QSqlDatabase database, const QString &table, const QString &column)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column) {
            return true;
        }
    }
    return false;
}

} // namespace

RequestLogService::RequestLogService(QSqlDatabase database)
    : m_database(database)
{
}

Result RequestLogService::ensureSchema() const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS request_log ("
            "request_id TEXT PRIMARY KEY,"
            "action TEXT NOT NULL DEFAULT '',"
            "code TEXT NOT NULL DEFAULT '',"
            "response_json TEXT NOT NULL,"
            "created_at TEXT NOT NULL)"))) {
        return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
    }

    if (!columnExists(m_database, QStringLiteral("request_log"), QStringLiteral("action"))) {
        if (!query.exec(QStringLiteral("ALTER TABLE request_log ADD COLUMN action TEXT NOT NULL DEFAULT ''"))) {
            return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
        }
    }
    if (!columnExists(m_database, QStringLiteral("request_log"), QStringLiteral("code"))) {
        if (!query.exec(QStringLiteral("ALTER TABLE request_log ADD COLUMN code TEXT NOT NULL DEFAULT ''"))) {
            return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
        }
    }

    return Result::success();
}

Result RequestLogService::record(const QString &requestId, const QString &action, const ev::protocol::ResponseEnvelope &response) const
{
    const Result schemaResult = ensureSchema();
    if (!schemaResult.ok) {
        return schemaResult;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO request_log(request_id, action, code, response_json, created_at) "
        "VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(requestId);
    query.addBindValue(action);
    query.addBindValue(response.code);
    query.addBindValue(QString::fromUtf8(sanitizedResponseJson(response)));
    query.addBindValue(nowIso());
    if (!query.exec()) {
        return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
    }
    return Result::success();
}

Result RequestLogService::list(const QString &requestIdFilter, int limit, int offset, QJsonObject *responseData) const
{
    const Result schemaResult = ensureSchema();
    if (!schemaResult.ok) {
        return schemaResult;
    }
    if (limit < 1 || limit > 100 || offset < 0) {
        return Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("limit must be 1..100 and offset must be non-negative"));
    }

    const bool filtered = !requestIdFilter.trimmed().isEmpty();
    QSqlQuery countQuery(m_database);
    if (filtered) {
        countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM request_log WHERE request_id=?"));
        countQuery.addBindValue(requestIdFilter.trimmed());
    } else {
        countQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM request_log"));
    }
    if (!countQuery.exec() || !countQuery.next()) {
        return Result::failure(QStringLiteral("DB_ERROR"), countQuery.lastError().text());
    }

    QSqlQuery query(m_database);
    if (filtered) {
        query.prepare(QStringLiteral(
            "SELECT request_id, action, code, created_at FROM request_log "
            "WHERE request_id=? ORDER BY created_at DESC, request_id DESC LIMIT ? OFFSET ?"));
        query.addBindValue(requestIdFilter.trimmed());
    } else {
        query.prepare(QStringLiteral(
            "SELECT request_id, action, code, created_at FROM request_log "
            "ORDER BY created_at DESC, request_id DESC LIMIT ? OFFSET ?"));
    }
    query.addBindValue(limit);
    query.addBindValue(offset);
    if (!query.exec()) {
        return Result::failure(QStringLiteral("DB_ERROR"), query.lastError().text());
    }

    QJsonArray items;
    while (query.next()) {
        items.append(QJsonObject{
            {QStringLiteral("requestId"), query.value(0).toString()},
            {QStringLiteral("action"), query.value(1).toString()},
            {QStringLiteral("code"), query.value(2).toString()},
            {QStringLiteral("createdAt"), query.value(3).toString()}
        });
    }

    if (responseData) {
        responseData->insert(QStringLiteral("items"), items);
        responseData->insert(QStringLiteral("total"), countQuery.value(0).toInt());
        responseData->insert(QStringLiteral("limit"), limit);
        responseData->insert(QStringLiteral("offset"), offset);
    }
    return Result::success();
}
