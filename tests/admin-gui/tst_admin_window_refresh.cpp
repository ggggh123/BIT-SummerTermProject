#include "app/AppContext.h"
#include "protocol/Envelope.h"
#include "services/RequestLogService.h"
#include "ui/MainWindow.h"

#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUuid>

namespace {

quint16 availablePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return probe.serverPort();
}

QString selectedFirstColumn(QTableWidget *table)
{
    const int row = table->currentRow();
    return row >= 0 && table->item(row, 0) ? table->item(row, 0)->text() : QString();
}

int rowForFirstColumn(QTableWidget *table, const QString &value)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0) && table->item(row, 0)->text() == value) {
            return row;
        }
    }
    return -1;
}

QString moneyText(qint64 fen)
{
    return QStringLiteral("¥%1").arg(QString::number(fen / 100.0, 'f', 2));
}

} // namespace

class AdminWindowRefreshTest : public QObject
{
    Q_OBJECT

private slots:
    void currentPageRefreshesWithoutRebuildAndPreservesSelectionAndFilters()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString databasePath = tempDir.filePath(QStringLiteral("runtime.db"));
        QVERIFY(QFile::copy(QStringLiteral(EV_TEST_GOLDEN_DB), databasePath));

        AppContext::Options options;
        options.databasePath = databasePath;
        options.host = QStringLiteral("127.0.0.1");
        options.port = availablePort();
        options.snapshotPath = tempDir.filePath(QStringLiteral("snapshot.json"));
        AppContext context;
        const Result initialized = context.initialize(options);
        QVERIFY2(initialized.ok, qPrintable(initialized.message));

        const QString connectionName = QStringLiteral("admin-window-refresh-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());

        MainWindow window(&context);
        window.show();
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("adminTabs"));
        QVERIFY(tabs);
        auto *todayMetric = window.findChild<QLabel *>(QStringLiteral("todayRevenueMetric"));
        QVERIFY(todayMetric);

        const qint64 todayBefore = context.dashboardService()->summary()
                                       .value(QStringLiteral("revenue")).toObject()
                                       .value(QStringLiteral("todayRevenueFen")).toInteger();
        QSqlQuery query(database);
        const QString today = QDateTime::currentDateTimeUtc()
                                  .toTimeZone(QTimeZone("Asia/Shanghai"))
                                  .date().toString(Qt::ISODate);
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO orders(user_id,charger_id,status,reserved_at,started_at,ended_at,energy_kwh,amount_fen) "
            "SELECT u.id,c.id,'completed','%1T00:00:00+08:00','%1T00:00:00+08:00','%1T00:01:00+08:00',1.0,777 "
            "FROM users u CROSS JOIN chargers c LIMIT 1").arg(today)));
        const QString expectedToday = QStringLiteral("今日营收：") + moneyText(todayBefore + 777);
        QTRY_COMPARE_WITH_TIMEOUT(todayMetric->text(), expectedToday, 4000);

        tabs->setCurrentIndex(2);
        auto *chargerTable = window.findChild<QTableWidget *>(QStringLiteral("chargerManagementTable"));
        QVERIFY(chargerTable);
        QVERIFY(chargerTable->rowCount() > 0);
        chargerTable->selectRow(0);
        const QString chargerId = selectedFirstColumn(chargerTable);
        QVERIFY(!chargerId.isEmpty());
        const QString newStatus = chargerTable->item(0, 5)->text() == QStringLiteral("fault")
            ? QStringLiteral("idle") : QStringLiteral("fault");
        query.prepare(QStringLiteral("UPDATE chargers SET status=? WHERE id=?"));
        query.addBindValue(newStatus);
        query.addBindValue(chargerId.toInt());
        QVERIFY(query.exec());
        tabs->setCurrentIndex(0);
        tabs->setCurrentIndex(2);
        QCOMPARE(window.findChild<QTableWidget *>(QStringLiteral("chargerManagementTable")), chargerTable);
        QCOMPARE(selectedFirstColumn(chargerTable), chargerId);
        const int chargerRow = rowForFirstColumn(chargerTable, chargerId);
        QVERIFY(chargerRow >= 0);
        QCOMPARE(chargerTable->item(chargerRow, 5)->text(), newStatus);

        tabs->setCurrentIndex(4);
        auto *searchEdit = window.findChild<QLineEdit *>(QStringLiteral("userSearchEdit"));
        auto *userTable = window.findChild<QTableWidget *>(QStringLiteral("userManagementTable"));
        QVERIFY(searchEdit);
        QVERIFY(userTable);
        QVERIFY(userTable->rowCount() > 0);
        const QString mobile = userTable->item(0, 1)->text();
        searchEdit->setText(mobile);
        tabs->setCurrentIndex(0);
        tabs->setCurrentIndex(4);
        QCOMPARE(userTable->rowCount(), 1);
        userTable->selectRow(0);
        const QString userId = selectedFirstColumn(userTable);
        const QString nickname = QStringLiteral("刷新后昵称");
        query.prepare(QStringLiteral("UPDATE users SET nickname=? WHERE id=?"));
        query.addBindValue(nickname);
        query.addBindValue(userId.toInt());
        QVERIFY(query.exec());
        tabs->setCurrentIndex(0);
        tabs->setCurrentIndex(4);
        QCOMPARE(window.findChild<QTableWidget *>(QStringLiteral("userManagementTable")), userTable);
        QCOMPARE(searchEdit->text(), mobile);
        QCOMPARE(selectedFirstColumn(userTable), userId);
        QCOMPARE(userTable->item(0, 2)->text(), nickname);

        const QString requestId = QStringLiteral("window-refresh-log");
        const ev::protocol::ResponseEnvelope response{
            requestId, true, QStringLiteral("OK"), QStringLiteral("success"), QJsonObject{}};
        QVERIFY(context.requestLogService()->record(
            requestId, QStringLiteral("system.health"), response).ok);
        tabs->setCurrentIndex(6);
        auto *requestTable = window.findChild<QTableWidget *>(QStringLiteral("requestLogTable"));
        QVERIFY(requestTable);
        QVERIFY(rowForFirstColumn(requestTable, requestId) >= 0);

        QVERIFY(query.exec(QStringLiteral("UPDATE snapshot_meta SET version=version+1 WHERE id=1")));
        QSqlQuery versionQuery(database);
        QVERIFY(versionQuery.exec(QStringLiteral("SELECT version FROM snapshot_meta WHERE id=1")));
        QVERIFY(versionQuery.next());
        const QString expectedVersion = versionQuery.value(0).toString();
        tabs->setCurrentIndex(7);
        auto *healthTable = window.findChild<QTableWidget *>(QStringLiteral("healthTable"));
        QVERIFY(healthTable);
        QCOMPARE(healthTable->item(2, 2)->text(), expectedVersion);

        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
};

QTEST_MAIN(AdminWindowRefreshTest)
#include "tst_admin_window_refresh.moc"
