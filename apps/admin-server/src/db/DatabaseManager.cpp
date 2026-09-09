#include "db/DatabaseManager.h"
#include "core/BusinessTime.h"
#include "db/SqlTransaction.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>
#include <QUuid>

namespace {

QString adminPasswordHash(const QString &password)
{
    const QByteArray digest = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

QString nowIso()
{
    return BusinessTime::now();
}

QString projectSourceDir()
{
#ifdef EV_PROJECT_SOURCE_DIR
    return QStringLiteral(EV_PROJECT_SOURCE_DIR);
#else
    return QDir::currentPath();
#endif
}

QString schemaPath()
{
    const QString configured = QDir(projectSourceDir()).filePath(QStringLiteral("database/schema.sql"));
    if (QFile::exists(configured)) {
        return configured;
    }

    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        const QString candidate = dir.filePath(QStringLiteral("database/schema.sql"));
        if (QFile::exists(candidate)) {
            return candidate;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return configured;
}

QString stripLineComments(const QString &script)
{
    QString cleaned;
    const QStringList lines = script.split(QLatin1Char('\n'));
    for (QString line : lines) {
        const int commentStart = line.indexOf(QStringLiteral("--"));
        if (commentStart >= 0) {
            line = line.left(commentStart);
        }
        cleaned += line;
        cleaned += QLatin1Char('\n');
    }
    return cleaned;
}
QStringList splitSqlStatements(const QString &script)
{
    QStringList statements;
    QString current;
    bool inSingleQuote = false;

    const QString cleaned = stripLineComments(script);
    for (const QChar ch : cleaned) {
        if (ch == QLatin1Char('\'')) {
            inSingleQuote = !inSingleQuote;
        }
        if (ch == QLatin1Char(';') && !inSingleQuote) {
            const QString trimmed = current.trimmed();
            if (!trimmed.isEmpty()) {
                statements.append(trimmed);
            }
            current.clear();
            continue;
        }
        current.append(ch);
    }

    const QString trimmed = current.trimmed();
    if (!trimmed.isEmpty()) {
        statements.append(trimmed);
    }
    return statements;
}

int scalarInt(QSqlDatabase database, const QString &sql)
{
    QSqlQuery query(database);
    if (!query.exec(sql) || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

} // namespace

DatabaseManager::DatabaseManager()
    : m_connectionName(QStringLiteral("management-") + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

DatabaseManager::~DatabaseManager()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        { auto db = QSqlDatabase::database(m_connectionName, false); db.close(); }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

QString DatabaseManager::resolvePath(const QString &databasePath)
{
    if (databasePath.trimmed().isEmpty()) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dataDir.isEmpty()) {
            dataDir = QDir::currentPath() + QStringLiteral("/runtime");
        }
        return QDir(dataDir).absoluteFilePath(QStringLiteral("charging_platform_server_data_v1.db"));
    }
    return QFileInfo(QDir::cleanPath(databasePath)).absoluteFilePath();
}

// 打开 SQLite 连接：整库生命周期唯一的入口（服务端是唯一 writer）。
Result DatabaseManager::open(const QString &databasePath)
{
    m_databasePath = resolvePath(databasePath);
    QDir().mkpath(QFileInfo(m_databasePath).absolutePath());

    QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
        ? QSqlDatabase::database(m_connectionName)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);

    db.setDatabaseName(m_databasePath);
    if (!db.open()) {
        return databaseFailure(db.lastError());
    }

    execSql(QStringLiteral("PRAGMA foreign_keys = ON"));   // SQLite 默认不检查外键，必须显式开启
    execSql(QStringLiteral("PRAGMA busy_timeout = 3000")); // 遇锁等 3 秒再失败，容忍瞬时争用
    execSql(QStringLiteral("PRAGMA journal_mode = WAL"));  // WAL：读写并发，写不阻塞读

    Result migration = migrate();   // 首启执行 schema.sql 建表
    if (!migration.ok) {
        return migration;
    }
    return seed();                  // 空库时灌入演示种子数据
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

QString DatabaseManager::databasePath() const
{
    return m_databasePath;
}

// 迁移：schema_version 表存在即认为库已初始化，否则逐条执行 schema.sql。
Result DatabaseManager::migrate()
{
    QSqlQuery exists(database());
    exists.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'"));
    if (!exists.exec()) {
        return databaseFailure(exists.lastError());
    }
    if (exists.next()) {
        return Result::success();
    }

    QFile file(schemaPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return Result::failure(QStringLiteral("DB_ERROR"), QStringLiteral("无法读取数据库结构文件：") + file.fileName());
    }

    const QString script = QString::fromUtf8(file.readAll());
    for (const QString &statement : splitSqlStatements(script)) {
        QString error;
        if (!execSql(statement, &error)) {
            return Result::failure(QStringLiteral("DB_ERROR"), error);
        }
    }
    return Result::success();
}

// 种子数据：空库时灌入 6 站 48 桩、30 用户和默认 admin（密码 sha256）。
// 逐表判空/INSERT OR IGNORE，保证重复启动不会重复插入。
Result DatabaseManager::seed()
{
    const QString timestamp = nowIso();

    if (scalarInt(database(), QStringLiteral("SELECT COUNT(*) FROM admins")) == 0) {
        QSqlQuery query(database());
        query.prepare(QStringLiteral("INSERT INTO admins(username, password_hash, created_at) VALUES(?, ?, ?)"));
        query.addBindValue(QStringLiteral("admin"));
        query.addBindValue(adminPasswordHash(QStringLiteral("123456")));
        query.addBindValue(timestamp);
        if (!query.exec()) {
            return databaseFailure(query.lastError());
        }
    }

    if (scalarInt(database(), QStringLiteral("SELECT COUNT(*) FROM stations")) == 0) {
        const QStringList stationRows = {
            QStringLiteral("'朝阳公园充电站', '北京市朝阳区朝阳公园南路1号', 39.9337, 116.4710, 150"),
            QStringLiteral("'望京充电站', '北京市朝阳区望京街10号', 39.9960, 116.4700, 120"),
            QStringLiteral("'中关村充电站', '北京市海淀区中关村大街27号', 39.9830, 116.3150, 150"),
            QStringLiteral("'五道口充电站', '北京市海淀区成府路28号', 39.9920, 116.3370, 120"),
            QStringLiteral("'奥林匹克充电站', '北京市朝阳区北辰东路15号', 40.0000, 116.3900, 120"),
            QStringLiteral("'亦庄充电站', '北京市大兴区荣华中路8号', 39.7960, 116.5060, 150")
        };
        for (const QString &row : stationRows) {
            QString error;
            const QString sql = QStringLiteral("INSERT INTO stations(name, address, latitude, longitude, price_fen_per_kwh, forecast_enabled, created_at) VALUES(%1, 1, '%2')").arg(row, timestamp);
            if (!execSql(sql, &error)) {
                return Result::failure(QStringLiteral("DB_ERROR"), error);
            }
        }
    }

    if (scalarInt(database(), QStringLiteral("SELECT COUNT(*) FROM chargers")) == 0) {
        int code = 1001;
        for (int stationId = 1; stationId <= 6; ++stationId) {
            for (int slot = 0; slot < 8; ++slot) {
                QSqlQuery query(database());
                query.prepare(QStringLiteral("INSERT INTO chargers(station_id, code, type, power_kw, status, charge_count, total_duration_sec, updated_at) VALUES(?, ?, ?, ?, ?, 0, 0, ?)"));
                query.addBindValue(stationId);
                query.addBindValue(QString::number(code++));
                query.addBindValue(slot < 4 ? QStringLiteral("fast") : QStringLiteral("slow"));
                query.addBindValue(slot < 4 ? 60.0 : 30.0);
                query.addBindValue(QStringLiteral("idle"));
                query.addBindValue(timestamp);
                if (!query.exec()) {
                    return databaseFailure(query.lastError());
                }
            }
        }
        execSql(QStringLiteral("UPDATE chargers SET status='fault' WHERE code='1010'"));
    }

    for (int userIndex = 0; userIndex < 30; ++userIndex) {
        const QString mobile = QStringLiteral("13800138%1").arg(userIndex, 3, 10, QLatin1Char('0'));
        QSqlQuery query(database());
        query.prepare(QStringLiteral("INSERT OR IGNORE INTO users(mobile, nickname, avatar_path, balance_fen, status, registered_at) VALUES(?, ?, '', ?, 'active', ?)"));
        query.addBindValue(mobile);
        query.addBindValue(QStringLiteral("用户") + mobile.right(4));
        query.addBindValue(userIndex == 0 ? 50000 : 10000 + userIndex * 1000);
        query.addBindValue(timestamp);
        if (!query.exec()) {
            return databaseFailure(query.lastError());
        }
    }

    return Result::success();
}

bool DatabaseManager::execSql(const QString &sql, QString *errorMessage)
{
    QSqlQuery query(database());
    if (query.exec(sql)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}
