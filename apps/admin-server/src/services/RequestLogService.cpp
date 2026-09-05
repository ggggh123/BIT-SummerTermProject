#include "services/RequestLogService.h"
#include "core/BusinessTime.h"
#include "db/SqlTransaction.h"

#include "protocol/JsonEnvelope.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>

namespace {

QString nowIso()
{
    return BusinessTime::now();
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
        return databaseFailure(query.lastError());
    }

    if (!columnExists(m_database, QStringLiteral("request_log"), QStringLiteral("action"))) {
        if (!query.exec(QStringLiteral("ALTER TABLE request_log ADD COLUMN action TEXT NOT NULL DEFAULT ''"))) {
            return databaseFailure(query.lastError());
        }
    }
    if (!columnExists(m_database, QStringLiteral("request_log"), QStringLiteral("code"))) {
        if (!query.exec(QStringLiteral("ALTER TABLE request_log ADD COLUMN code TEXT NOT NULL DEFAULT ''"))) {
            return databaseFailure(query.lastError());
        }
    }
    for (const QString &column : {QStringLiteral("actor"), QStringLiteral("request_hash")}) {
        if (!columnExists(m_database, QStringLiteral("request_log"), column)
            && !query.exec(QStringLiteral("ALTER TABLE request_log ADD COLUMN %1 TEXT NOT NULL DEFAULT ''").arg(column))) {
            return databaseFailure(query.lastError());
        }
    }

    return Result::success();
}

QByteArray RequestLogService::execute(const ev::protocol::RequestEnvelope &request, const QString &actor,
                                    const std::function<ev::protocol::ResponseEnvelope()> &business,
                                    bool twoPhase) const
{
    const auto failure = [&](const Result &result) {
        return ev::protocol::toJson({request.requestId, false, result.code, result.message, QJsonObject{}});
    };
    const Result schema = ensureSchema();
    if (!schema.ok) return failure(schema);

    // 只保存稳定主体和规范 payload 的哈希；token 从不作为持久化身份或日志字段。
    const QByteArray bytes = QJsonDocument(request.payload).toJson(QJsonDocument::Compact);
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    SqlTransaction transaction(m_database, true);
    if (!twoPhase && !transaction.transaction()) return failure(databaseFailure(transaction.lastError()));
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT actor, action, request_hash, response_json FROM request_log WHERE request_id=?"));
    query.addBindValue(request.requestId);
    if (!query.exec()) return failure(databaseFailure(query.lastError()));
    if (query.next()) {
        if (query.value(0).toString() != actor) {
            return failure(Result::failure(QStringLiteral("FORBIDDEN"), QStringLiteral("requestId 已被其他请求使用")));
        }
        if (query.value(1).toString() != request.action || query.value(2).toString() != hash) {
            return failure(Result::failure(QStringLiteral("INVALID_REQUEST"), QStringLiteral("requestId 与首次请求不一致")));
        }
        return query.value(3).toString().toUtf8();
    }
    query.finish();
    const ev::protocol::ResponseEnvelope response = business();
    if (!response.ok) return ev::protocol::toJson(response);

    // forecast.publish 在业务内已提交预测批次并尝试 snapshot；这里只原子保存最终 ACK。
    if (twoPhase && !transaction.transaction()) return failure(databaseFailure(transaction.lastError()));
    const QByteArray responseBytes = sanitizedResponseJson(response);
    query.prepare(QStringLiteral("INSERT INTO request_log(request_id,action,code,response_json,created_at,actor,request_hash) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(request.requestId);
    query.addBindValue(request.action);
    query.addBindValue(response.code);
    query.addBindValue(QString::fromUtf8(responseBytes));
    query.addBindValue(nowIso());
    query.addBindValue(actor);
    query.addBindValue(hash);
    if (!query.exec()) return failure(databaseFailure(query.lastError()));
    if (!transaction.commit()) return failure(databaseFailure(transaction.lastError()));
    return responseBytes;
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
        return databaseFailure(query.lastError());
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
        return databaseFailure(countQuery.lastError());
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
        return databaseFailure(query.lastError());
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
