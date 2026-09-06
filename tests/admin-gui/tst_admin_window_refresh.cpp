#include "app/AppContext.h"
#include "protocol/Envelope.h"
#include "protocol/JsonEnvelope.h"
#include <QEventLoop>
#include "services/DashboardService.h"
#include "services/RequestLogService.h"
#include "ui/MainWindow.h"
#include "ui/LoginDialog.h"
#include <QDoubleSpinBox>
#include <QGroupBox>

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

QString loginAdmin(AppContext &context)
{
    QEventLoop loop;
    QString token;
    context.executeLocal({1,QUuid::createUuid().toString(),"admin.login",{},{{"username","admin"},{"password","123456"}}},
        &loop,[&](const QByteArray &bytes) {
            token = ev::protocol::parseResponse(bytes).data.toObject().value("token").toString();
            loop.quit();
        });
    QTimer::singleShot(5000,&loop,&QEventLoop::quit);
    loop.exec();
    return token;
}
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
    void failedResetAckKeepsRequestIdForConfirmedRetry()
    {
        QTemporaryDir dir; AppContext context;
        AppContext::Options options;
        options.port=0; options.databasePath=dir.filePath("retry.db"); options.snapshotPath=dir.filePath("snapshot.json");
        QVERIFY(context.initialize(options).ok);
        const auto admin=loginAdmin(context); QVERIFY(!admin.isEmpty());
        MainWindow window(&context,admin); window.show();
        window.findChild<QTabWidget *>("adminTabs")->setCurrentIndex(7);
        auto *reset=window.findChild<QPushButton *>("demoResetButton"); QVERIFY(reset);
        auto db=QSqlDatabase::addDatabase("QSQLITE","gui-reset-retry");
        db.setDatabaseName(options.databasePath); QVERIFY(db.open());
        {
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER reject_gui_ack BEFORE INSERT ON request_log WHEN NEW.action='demo.reset' "
                "BEGIN SELECT RAISE(ABORT,'test ACK failure'); END"));
            auto confirm=[&] {
                QTimer::singleShot(0,&window,[] {
                    if (auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                        box->button(QMessageBox::Yes)->click();
                });
            };
            bool failureSeen=false, enabledOnFailure=false;
            QTimer failureDialog;
            connect(&failureDialog,&QTimer::timeout,&window,[&] {
                if (auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                    box && box->windowTitle()==QStringLiteral("复位未完成")) {
                    failureSeen=true; enabledOnFailure=reset->isEnabled(); box->accept(); failureDialog.stop();
                }
            });
            failureDialog.start(10);
            confirm(); reset->click();
            QVERIFY(!reset->isEnabled());
            const auto requestId=reset->property("requestId").toString(); QVERIFY(!requestId.isEmpty());
            QTRY_VERIFY(failureSeen); QVERIFY(enabledOnFailure); QVERIFY(reset->isEnabled());
            QCOMPARE(reset->property("requestId").toString(),requestId);
            query.prepare("SELECT request_id,state,reset_at,golden_hash,snapshot_version FROM demo_reset_receipts");
            QVERIFY(query.exec()); QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(),requestId); QCOMPARE(query.value(1).toString(),QString("pending"));
            const auto resetAt=query.value(2); const auto hash=query.value(3); const auto version=query.value(4);
            QVERIFY(!query.next()); query.finish();
            QVERIFY(query.exec("DROP TRIGGER reject_gui_ack"));
            // 核心复位后写入新业务；重试 final ACK 不能再次清库。
            QByteArray loginBytes,rechargeBytes;
            QObject replyScope;
            context.executeLocal({1,"retry-user","auth.user_login",{},{{"mobile","13800138000"}}},&replyScope,
                [&](auto bytes) { loginBytes=bytes; });
            QTRY_VERIFY(!loginBytes.isEmpty());
            const auto user=ev::protocol::parseResponse(loginBytes).data.toObject();
            const auto balance=user.value("user").toObject().value("balanceFen").toInteger();
            context.executeLocal({1,"between-reset-attempts","wallet.recharge",user.value("token").toString(),{{"amountFen",123}}},
                &replyScope,[&](auto bytes) { rechargeBytes=bytes; });
            QTRY_VERIFY(!rechargeBytes.isEmpty()); QVERIFY(ev::protocol::parseResponse(rechargeBytes).ok);
            confirm(); reset->click();
            QVERIFY(!reset->isEnabled()); QCOMPARE(reset->property("requestId").toString(),requestId);
            QTRY_VERIFY(reset->isEnabled()); QTRY_VERIFY(reset->property("requestId").toString().isEmpty());
            QVERIFY(query.exec("SELECT request_id,state,reset_at,golden_hash,snapshot_version FROM demo_reset_receipts"));
            QVERIFY(query.next()); QCOMPARE(query.value(0).toString(),requestId); QCOMPARE(query.value(1).toString(),QString("final"));
            QCOMPARE(query.value(2),resetAt); QCOMPARE(query.value(3),hash); QCOMPARE(query.value(4),version);
            QVERIFY(!query.next());
            QVERIFY(query.exec("SELECT balance_fen FROM users WHERE mobile='13800138000'")); QVERIFY(query.next());
            QCOMPARE(query.value(0).toLongLong(),balance+123);
            QVERIFY(query.exec("SELECT version FROM snapshot_meta")); QVERIFY(query.next()); QCOMPARE(query.value(0),version);
            QVERIFY(query.exec("SELECT COUNT(*) FROM request_log WHERE action='demo.reset' AND code='OK'"));
            QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(),1);
        }
        db.close(); db={}; QSqlDatabase::removeDatabase("gui-reset-retry");
    }
    void guiLoginAndMutationsUseQueuedAuthenticatedDispatcher()
    {
        QTemporaryDir dir;
        AppContext context;
        AppContext::Options options;
        options.port = 0;
        options.databasePath = dir.filePath("gui.db");
        options.snapshotPath = dir.filePath("snapshot.json");
        QVERIFY(context.initialize(options).ok);
        LoginDialog login(&context);
        const auto fields = login.findChildren<QLineEdit *>();
        QCOMPARE(fields.size(),2);
        fields.at(1)->setText("123456");
        QVERIFY(QMetaObject::invokeMethod(&login,"tryLogin",Qt::DirectConnection));
        QVERIFY(!login.isEnabled());
        QTRY_COMPARE(login.result(),int(QDialog::Accepted));
        QVERIFY(!login.adminToken().isEmpty());
        MainWindow window(&context,login.adminToken());
        window.show();
        auto *tabs = window.findChild<QTabWidget *>("adminTabs");
        auto db = QSqlDatabase::addDatabase("QSQLITE","gui-audit");
        db.setDatabaseName(options.databasePath); QVERIFY(db.open());
        auto auditCount = [&](const QString &action) {
            QSqlQuery query(db);
            query.prepare("SELECT COUNT(*) FROM request_log WHERE action=? AND code='OK' AND actor LIKE 'admin:%'");
            query.addBindValue(action);
            if (!query.exec() || !query.next()) return -1;
            return query.value(0).toInt();
        };
        tabs->setCurrentIndex(3);
        auto *form = window.findChild<QGroupBox *>();
        QVERIFY(form);
        const auto edits = form->findChildren<QLineEdit *>(QString(),Qt::FindDirectChildrenOnly);
        QCOMPARE(edits.size(),2);
        edits.at(0)->setText(QStringLiteral("队列测试站"));
        edits.at(1)->setText(QStringLiteral("测试地址"));
        const auto doubles = form->findChildren<QDoubleSpinBox *>();
        QCOMPARE(doubles.size(),3);
        doubles.at(2)->setValue(1.5);
        const auto counts = form->findChildren<QSpinBox *>();
        QCOMPARE(counts.size(),2);
        counts.at(0)->setValue(1);
        auto *create = buttonWithText(&window,QStringLiteral("新增站点"));
        create->click();
        QVERIFY(!create->isEnabled());
        QTRY_COMPARE(auditCount("admin.station_create"),1);
        QTRY_VERIFY(create->isEnabled());
        auto confirm = [] {
            QTimer::singleShot(0,[] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    if (auto *yes = box->button(QMessageBox::Yes)) yes->click();
            });
        };
        tabs->setCurrentIndex(4);
        auto *users = window.findChild<QTableWidget *>("userManagementTable");
        QTRY_VERIFY(users->rowCount() > 0);
        users->selectRow(0);
        auto *freeze = buttonWithText(&window,QStringLiteral("冻结"));
        confirm(); freeze->click();
        QVERIFY(!freeze->isEnabled());
        QTRY_COMPARE(auditCount("admin.user_set_status"),1);
        QTRY_VERIFY(freeze->isEnabled());
        tabs->setCurrentIndex(2);
        auto *chargers = window.findChild<QTableWidget *>("chargerManagementTable");
        QTRY_VERIFY(rowForFirstColumn(chargers,"10") >= 0);
        chargers->selectRow(rowForFirstColumn(chargers,"10"));
        auto *restart = buttonWithText(&window,QStringLiteral("远程重启故障桩"));
        confirm(); restart->click();
        QVERIFY(!restart->isEnabled());
        QTRY_COMPARE(auditCount("admin.charger_restart"),1);
        QTRY_VERIFY(restart->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(chargers->item(rowForFirstColumn(chargers,"10"),5)->text(),QString("idle"),4000);
        QCOMPARE(auditCount("admin.charger_restart"),1);
        tabs->setCurrentIndex(7);
        auto *reset=window.findChild<QPushButton *>("demoResetButton");
        QVERIFY(reset);
        QTimer::singleShot(0, &window, [] {
            if (auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(QMessageBox::No)->click();
        });
        reset->click();
        QCOMPARE(auditCount("demo.reset"),0);
        int versionBefore=0;
        {
            QSqlQuery version(db);
            QVERIFY(version.exec("SELECT version FROM snapshot_meta WHERE id=1"));
            QVERIFY(version.next()); versionBefore=version.value(0).toInt();
        }
        confirm(); reset->click();
        QVERIFY(!reset->isEnabled());
        QTRY_COMPARE(auditCount("demo.reset"),1);
        QTRY_VERIFY(reset->isEnabled());
        auto *health=window.findChild<QTableWidget *>("healthTable");
        QTRY_COMPARE(health->item(2,2)->text(),QString::number(versionBefore+1));
        db.close(); db = {}; QSqlDatabase::removeDatabase("gui-audit");
    }

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

        const auto adminToken = loginAdmin(context);
        QVERIFY(!adminToken.isEmpty());
        MainWindow window(&context, adminToken);
        window.show();
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("adminTabs"));
        QVERIFY(tabs);
        auto *todayMetric = window.findChild<QLabel *>(QStringLiteral("todayRevenueMetric"));
        QVERIFY(todayMetric);

        const qint64 todayBefore = DashboardService(database).summary()
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
        QTRY_VERIFY(chargerTable->rowCount() > 0);
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
        QTRY_COMPARE(chargerTable->item(chargerRow, 5)->text(), newStatus);

        tabs->setCurrentIndex(4);
        auto *searchEdit = window.findChild<QLineEdit *>(QStringLiteral("userSearchEdit"));
        auto *userTable = window.findChild<QTableWidget *>(QStringLiteral("userManagementTable"));
        QVERIFY(searchEdit);
        QVERIFY(userTable);
        QTRY_VERIFY(userTable->rowCount() > 0);
        const QString mobile = userTable->item(0, 1)->text();
        searchEdit->setText(mobile);
        tabs->setCurrentIndex(0);
        tabs->setCurrentIndex(4);
        QTRY_COMPARE(userTable->rowCount(), 1);
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
        QTRY_COMPARE(userTable->item(0, 2)->text(), nickname);

        const QString requestId = QStringLiteral("window-refresh-log");
        const ev::protocol::ResponseEnvelope response{
            requestId, true, QStringLiteral("OK"), QStringLiteral("success"), QJsonObject{}};
        QVERIFY(RequestLogService(database).record(
            requestId, QStringLiteral("system.health"), response).ok);
        tabs->setCurrentIndex(6);
        auto *requestTable = window.findChild<QTableWidget *>(QStringLiteral("requestLogTable"));
        QVERIFY(requestTable);
        QTRY_VERIFY(rowForFirstColumn(requestTable, requestId) >= 0);

        QVERIFY(query.exec(QStringLiteral("UPDATE snapshot_meta SET version=version+1 WHERE id=1")));
        QSqlQuery versionQuery(database);
        QVERIFY(versionQuery.exec(QStringLiteral("SELECT version FROM snapshot_meta WHERE id=1")));
        QVERIFY(versionQuery.next());
        const QString expectedVersion = versionQuery.value(0).toString();
        tabs->setCurrentIndex(7);
        auto *healthTable = window.findChild<QTableWidget *>(QStringLiteral("healthTable"));
        QVERIFY(healthTable);
        QTRY_COMPARE_WITH_TIMEOUT(healthTable->item(2, 2)->text(), expectedVersion, 4000);

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

        const auto adminToken = loginAdmin(context);
        QVERIFY(!adminToken.isEmpty());
        MainWindow window(&context, adminToken);
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
        QTRY_COMPARE(userTable->rowCount(), 20);
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
        QTRY_COMPARE(userTable->rowCount(), 20);
        QTRY_COMPARE(userTable->item(0, 0)->text(), remainingUserId);

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
