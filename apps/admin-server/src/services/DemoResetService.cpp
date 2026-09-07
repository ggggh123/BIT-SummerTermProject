#include "services/DemoResetService.h"
#include "services/RequestPreflight.h"
#include "core/BusinessTime.h"
#include "dashboard/SnapshotWriter.h"
#include "db/SqlTransaction.h"
#include "protocol/JsonEnvelope.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QUrl>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace {
const QStringList tables = {"admins","users","stations","chargers","orders","telemetry",
    "station_hourly_history","forecast_runs","forecasts","events"};
const QString identity = QStringLiteral("{\"confirmation\":\"RESET_DEMO\"}");
QByteArray failure(const QString &id, const Result &result) {
    return ev::protocol::toJson({id,false,result.code,result.message,QJsonObject{}});
}
Result invalidGolden() { return Result::failure("INTERNAL_ERROR","黄金库校验或恢复失败，未复位业务数据"); }
struct Attachment {
    QSqlDatabase db;
    ~Attachment() { QSqlQuery q(db); q.exec("DETACH DATABASE reset_golden"); }
};
QString definition(QSqlDatabase db, const QString &schema, const QString &name) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT sql FROM %1.sqlite_master WHERE type='table' AND name=?").arg(schema));
    q.addBindValue(name);
    if (!q.exec() || !q.next()) return {};
    return q.value(0).toString().simplified();
}
QStringList indexes(QSqlDatabase db, const QString &schema, const QString &table) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT name,sql FROM %1.sqlite_master WHERE type='index' AND tbl_name=? AND sql IS NOT NULL ORDER BY name").arg(schema));
    q.addBindValue(table);
    if (!q.exec()) return {"invalid index schema"};
    QStringList result;
    while (q.next()) result.append(q.value(0).toString()+":"+q.value(1).toString().simplified());
    return result;
}
}

DemoResetService::DemoResetService(QSqlDatabase db, QString golden, QString hash, QString snapshotPath,
                                 std::function<void()> committed)
    : m_database(db), m_goldenPath(std::move(golden)), m_goldenHash(std::move(hash)),
      m_snapshotPath(std::move(snapshotPath)), m_resetCommitted(std::move(committed)) {}

Result DemoResetService::ensureSchema() const {
    QSqlQuery q(m_database);
    if (!q.exec("CREATE TABLE IF NOT EXISTS demo_reset_receipts ("
        "request_id TEXT PRIMARY KEY, actor TEXT NOT NULL, payload_identity TEXT NOT NULL,"
        "state TEXT NOT NULL CHECK(state IN ('pending','final')), reset_at TEXT NOT NULL,"
        "golden_hash TEXT NOT NULL, snapshot_version INTEGER NOT NULL,"
        "snapshot_ready INTEGER NOT NULL DEFAULT 0, ack BLOB,"
        "CHECK((state='pending' AND ack IS NULL) OR (state='final' AND ack IS NOT NULL)))"))
        return databaseFailure(q.lastError());
    return Result::success();
}

bool DemoResetService::validGoldenFile() const {
    if (!QRegularExpression("^[0-9a-f]{64}$").match(m_goldenHash).hasMatch()) return false;
    QFileInfo golden(m_goldenPath), runtime(m_database.databaseName());
    if (!golden.isFile() || golden.canonicalFilePath()==runtime.canonicalFilePath()) return false;
#ifdef Q_OS_UNIX
    struct stat a{},b{};
    if (::stat(QFile::encodeName(golden.absoluteFilePath()).constData(),&a)!=0
        || ::stat(QFile::encodeName(runtime.absoluteFilePath()).constData(),&b)!=0
        || (a.st_dev==b.st_dev && a.st_ino==b.st_ino)) return false;
#endif
    for (const auto &suffix : {"-wal","-shm","-journal"})
        if (QFileInfo::exists(m_goldenPath+suffix) || QFileInfo::exists(golden.canonicalFilePath()+suffix)) return false;
    QFile file(m_goldenPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) && QString::fromLatin1(hash.result().toHex())==m_goldenHash;
}

Result DemoResetService::restore(const QString &id, const QString &actor) const {
    if (!validGoldenFile()) return invalidGolden();
    QSqlQuery q(m_database);
    q.prepare("ATTACH DATABASE ? AS reset_golden");
    q.addBindValue(QUrl::fromLocalFile(QFileInfo(m_goldenPath).canonicalFilePath()).toString(QUrl::FullyEncoded)+"?mode=ro&immutable=1");
    if (!q.exec()) return invalidGolden();
    Attachment attachment{m_database};
    if (!q.exec("PRAGMA reset_golden.integrity_check") || !q.next() || q.value(0).toString()!="ok") return invalidGolden();
    if (!q.exec("PRAGMA reset_golden.foreign_key_check") || q.next()) return invalidGolden();
    if (!q.exec("SELECT version FROM reset_golden.schema_version") || !q.next() || q.value(0).toInt()!=1 || q.next()) return invalidGolden();
    if (!q.exec("SELECT version FROM main.schema_version") || !q.next() || q.value(0).toInt()!=1 || q.next()) return invalidGolden();
    for (const auto &table : tables + QStringList{"schema_version","snapshot_meta"}) {
        const auto source=definition(m_database,"reset_golden",table);
        if (source.isEmpty() || source!=definition(m_database,"main",table)) return invalidGolden();
        if (indexes(m_database,"reset_golden",table)!=indexes(m_database,"main",table)) return invalidGolden();
    }
    q.finish();
    SqlTransaction tx(m_database,true);
    if (!tx.transaction()) return databaseFailure(tx.lastError());
    // 依赖方向固定；外键始终开启，schema_version 和 receipts 均不进入清表集。
    for (auto it=tables.crbegin();it!=tables.crend();++it)
        if (!q.exec("DELETE FROM main."+*it)) return databaseFailure(q.lastError());
    for (const auto &table : tables)
        if (!q.exec("INSERT INTO main."+table+" SELECT * FROM reset_golden."+table)) return databaseFailure(q.lastError());
    if (!q.exec("DELETE FROM request_log")) return databaseFailure(q.lastError());
    if (!q.exec("UPDATE snapshot_meta SET version=version+1 WHERE id=1") || q.numRowsAffected()!=1) return invalidGolden();
    if (!q.exec("PRAGMA main.foreign_key_check") || q.next()) return invalidGolden();
    q.prepare("INSERT INTO demo_reset_receipts(request_id,actor,payload_identity,state,reset_at,golden_hash,snapshot_version) "
              "SELECT ?,?,?,'pending',?,?,version FROM snapshot_meta WHERE id=1");
    q.addBindValue(id); q.addBindValue(actor); q.addBindValue(identity);
    q.addBindValue(BusinessTime::now()); q.addBindValue(m_goldenHash);
    if (!q.exec()) return databaseFailure(q.lastError());
    if (!validGoldenFile()) return invalidGolden();
    if (!tx.commit()) return databaseFailure(tx.lastError());
    m_resetCommitted();
    return Result::success();
}

QByteArray DemoResetService::rejectReservedId(const ev::protocol::RequestEnvelope &request, const QString &actor) const {
    QSqlQuery q(m_database);
    q.prepare("SELECT actor FROM demo_reset_receipts WHERE request_id=?"); q.addBindValue(request.requestId);
    if (!q.exec()) return failure(request.requestId,databaseFailure(q.lastError()));
    if (!q.next()) return {};
    return failure(request.requestId,Result::failure(q.value(0).toString()==actor ? "INVALID_REQUEST":"FORBIDDEN",
        "requestId 已被复位请求使用"));
}

bool DemoResetService::snapshot(qint64 version) const {
    QSqlQuery q(m_database);
    if (!q.exec("SELECT version FROM snapshot_meta WHERE id=1") || !q.next() || q.value(0).toLongLong()!=version) return false;
    q.finish();
    return SnapshotWriter(m_database).write(m_snapshotPath);
}

QByteArray DemoResetService::execute(const ev::protocol::RequestEnvelope &request, const QString &actor) const {
    const auto fail=[&](const Result &r) { return failure(request.requestId,r); };
    const auto validation=RequestPreflight::payload(ev::actions::DemoReset,request.payload);
    if (!validation.ok) return fail(validation);
    QSqlQuery receipt(m_database);
    receipt.prepare("SELECT actor,payload_identity,state,reset_at,golden_hash,snapshot_version,ack FROM demo_reset_receipts WHERE request_id=?");
    receipt.addBindValue(request.requestId);
    if (!receipt.exec()) return fail(databaseFailure(receipt.lastError()));
    if (!receipt.next()) {
        receipt.finish();
        QSqlQuery existing(m_database);
        existing.prepare("SELECT actor FROM request_log WHERE request_id=?"); existing.addBindValue(request.requestId);
        if (!existing.exec()) return fail(databaseFailure(existing.lastError()));
        if (existing.next()) return fail(Result::failure(existing.value(0).toString()==actor ? "INVALID_REQUEST":"FORBIDDEN","requestId 已被其他请求使用"));
        existing.finish();
        const auto restored=restore(request.requestId,actor);
        if (!restored.ok) return fail(restored);
        if (!receipt.exec() || !receipt.next()) return fail(databaseFailure(receipt.lastError()));
    }
    if (receipt.value(0).toString()!=actor) return fail(Result::failure("FORBIDDEN","requestId 已被其他请求使用"));
    if (receipt.value(1).toString()!=identity) return fail(Result::failure("INVALID_REQUEST","requestId 与首次请求不一致"));
    if (receipt.value(2).toString()=="final") return receipt.value(6).toByteArray();
    const auto resetAt=receipt.value(3).toString(), goldenHash=receipt.value(4).toString();
    const auto version=receipt.value(5).toLongLong();
    receipt.finish();
    const bool ready=snapshot(version);
    const auto bytes=ev::protocol::toJson({request.requestId,true,"OK",ready ? "演示数据已复位":"演示数据已复位；快照尚未刷新，将按版本重试",
        QJsonObject{{"resetAt",resetAt},{"goldenHash",goldenHash}}});
    SqlTransaction tx(m_database,true);
    if (!tx.transaction()) return fail(databaseFailure(tx.lastError()));
    QSqlQuery q(m_database);
    q.prepare("UPDATE demo_reset_receipts SET state='final',ack=?,snapshot_ready=? WHERE request_id=? AND state='pending'");
    q.addBindValue(bytes); q.addBindValue(ready ? 1:0); q.addBindValue(request.requestId);
    if (!q.exec() || q.numRowsAffected()!=1) return fail(databaseFailure(q.lastError()));
    q.prepare("INSERT INTO request_log(request_id,action,code,response_json,created_at,actor,request_hash) VALUES(?,'demo.reset','OK',?,?,?,?)");
    q.addBindValue(request.requestId); q.addBindValue(QString::fromUtf8(bytes)); q.addBindValue(resetAt); q.addBindValue(actor);
    q.addBindValue(QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(),QCryptographicHash::Sha256).toHex()));
    if (!q.exec()) return fail(databaseFailure(q.lastError()));
    if (!tx.commit()) return fail(databaseFailure(tx.lastError()));
    return bytes;
}

void DemoResetService::retrySnapshot() const {
    QSqlQuery q(m_database);
    if (!q.exec("SELECT r.snapshot_version FROM demo_reset_receipts r JOIN snapshot_meta m ON m.id=1 AND m.version=r.snapshot_version "
                "WHERE r.snapshot_ready=0 LIMIT 1") || !q.next()) return;
    const auto version=q.value(0).toLongLong(); q.finish();
    if (!snapshot(version)) return;
    SqlTransaction tx(m_database,true);
    if (!tx.transaction()) return;
    q.prepare("UPDATE demo_reset_receipts SET snapshot_ready=1 WHERE snapshot_version=?"); q.addBindValue(version);
    if (q.exec()) tx.commit();
}
