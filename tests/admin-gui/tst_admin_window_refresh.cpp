#include "app/AppContext.h"
#include "protocol/Envelope.h"
#include "services/RequestLogService.h"
#include "ui/MainWindow.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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

QPushButton *buttonWithText(QWidget *root, const QString &text)
{
    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
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

    void removedStableKeyCannotRetargetUserAction()
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

        const QString connectionName = QStringLiteral("admin-window-retarget-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());

        MainWindow window(&context);
        window.show();
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("adminTabs"));
        auto *offsetSpin = window.findChild<QSpinBox *>(QStringLiteral("userOffsetSpin"));
        auto *userTable = window.findChild<QTableWidget *>(QStringLiteral("userManagementTable"));
        auto *freezeButton = buttonWithText(&window, QStringLiteral("冻结"));
        QVERIFY(tabs);
        QVERIFY(offsetSpin);
        QVERIFY(userTable);
        QVERIFY(freezeButton);

        tabs->setCurrentIndex(4);
        QCOMPARE(userTable->rowCount(), 20);
        const QString removedUserId = userTable->item(0, 0)->text();
        const QString remainingUserId = userTable->item(1, 0)->text();
        QVERIFY(removedUserId != remainingUserId);

        QSqlQuery query(database);
        query.prepare(QStringLiteral("UPDATE users SET status='active' WHERE id IN (?,?)"));
        query.addBindValue(removedUserId.toInt());
        query.addBindValue(remainingUserId.toInt());
        QVERIFY(query.exec());

        userTable->selectRow(0);
        QCOMPARE(selectedFirstColumn(userTable), removedUserId);
        offsetSpin->setValue(1);
        tabs->setCurrentIndex(0);
        tabs->setCurrentIndex(4);
        QCOMPARE(userTable->rowCount(), 20);
        QCOMPARE(userTable->item(0, 0)->text(), remainingUserId);

        const bool selectionWasCleared = userTable->selectedRanges().isEmpty()
            && userTable->currentItem() == nullptr
            && userTable->currentRow() == -1;
        QTimer::singleShot(0, []() {
            if (auto *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (QAbstractButton *yesButton = messageBox->button(QMessageBox::Yes)) {
                    yesButton->click();
                } else {
                    messageBox->accept();
                }
            }
        });
        freezeButton->click();

        query.prepare(QStringLiteral("SELECT status FROM users WHERE id=?"));
        query.addBindValue(remainingUserId.toInt());
        QVERIFY(query.exec());
        QVERIFY(query.next());
        const QString remainingStatus = query.value(0).toString();
        QCOMPARE(remainingStatus, QStringLiteral("active"));
        QVERIFY2(selectionWasCleared, "旧稳定 key 消失后仍保留了指向新行的选择/当前索引");

        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
};

QTEST_MAIN(AdminWindowRefreshTest)
#include "tst_admin_window_refresh.moc"
