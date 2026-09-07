#include "app/AppContext.h"
#include "protocol/JsonEnvelope.h"
#include "protocol/FrameCodec.h"
#include <QTest>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QSqlQuery>
#include <QElapsedTimer>
#include <QTimer>

class DatabaseWorkerTest : public QObject {
    Q_OBJECT
private slots:
    void databaseWaitDoesNotBlockUiOrHealth() {
        QTemporaryDir dir;
        AppContext context;
        AppContext::Options options;
        options.databasePath = dir.filePath("test.db");
        options.snapshotPath = dir.filePath("snapshot.json");
        QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        options.port = probe.serverPort(); probe.close();
        QVERIFY(context.initialize(options).ok);
        QTcpSocket rechargeSocket, healthSocket;
        rechargeSocket.connectToHost(QHostAddress::LocalHost, context.port());
        healthSocket.connectToHost(QHostAddress::LocalHost, context.port());
        QTRY_COMPARE(rechargeSocket.state(), QAbstractSocket::ConnectedState);
        QTRY_COMPARE(healthSocket.state(), QAbstractSocket::ConnectedState);
        auto send = [](QTcpSocket &socket, const ev::protocol::RequestEnvelope &request) {
            const auto bytes = ev::protocol::toJson(request);
            socket.write(ev::protocol::encodeFrame(bytes)); socket.flush();
        };
        send(rechargeSocket, {1,"login","auth.user_login",{},{{"mobile","13800138000"}}});
        QTRY_VERIFY(rechargeSocket.bytesAvailable() > 4);
        ev::protocol::FrameDecoder decoder;
        auto frames = decoder.append(rechargeSocket.readAll());
        QCOMPARE(frames.size(), 1);
        auto login = ev::protocol::parseResponse(frames.first());
        QVERIFY(login.ok);
        const auto token = login.data.toObject().value("token").toString();
        auto lockDb = QSqlDatabase::addDatabase("QSQLITE", "test-lock");
        lockDb.setDatabaseName(options.databasePath); QVERIFY(lockDb.open());
        {
            QSqlQuery lock(lockDb); QVERIFY(lock.exec("BEGIN IMMEDIATE"));
            QElapsedTimer elapsed; elapsed.start();
            qint64 uiTick = -1;
            QObject timerScope; // 早退时先取消回调，再析构所捕获的局部对象。
            QTimer::singleShot(30, &timerScope, [&]{ uiTick = elapsed.elapsed(); });
            QTimer::singleShot(700, &timerScope, [&]{ lock.exec("ROLLBACK"); });
            send(rechargeSocket, {1,"recharge","wallet.recharge",token,{{"amountFen",100}}});
            QTest::qWait(50);
            send(healthSocket, {1,"health","system.health",{}, {}});
            QTRY_VERIFY_WITH_TIMEOUT(healthSocket.bytesAvailable() > 4, 500);
            QVERIFY2(uiTick >= 0 && uiTick < 500, qPrintable(QString::number(uiTick)));
            QVERIFY(elapsed.elapsed() < 500);
            ev::protocol::FrameDecoder healthDecoder;
            const auto health = ev::protocol::parseResponse(healthDecoder.append(healthSocket.readAll()).first());
            QCOMPARE(health.code, QString("OK"));
            QCOMPARE(health.data.toObject().value("schemaVersion").toInt(), 1);
            QTRY_VERIFY_WITH_TIMEOUT(rechargeSocket.bytesAvailable() > 4, 4000);
            const auto first = decoder.append(rechargeSocket.readAll()).first();
            QVERIFY(ev::protocol::parseResponse(first).ok);
            send(rechargeSocket, {1,"recharge","wallet.recharge",token,{{"amountFen",100}}});
            QTRY_VERIFY(rechargeSocket.bytesAvailable() > 4);
            QCOMPARE(decoder.append(rechargeSocket.readAll()).first(), first);
            QSqlQuery balance(lockDb); QVERIFY(balance.exec("SELECT balance_fen FROM users WHERE mobile='13800138000'"));
            QVERIFY(balance.next()); QCOMPARE(balance.value(0).toInt(), 50100);
        }
        lockDb.close(); lockDb = {}; QSqlDatabase::removeDatabase("test-lock");
    }
};
QTEST_GUILESS_MAIN(DatabaseWorkerTest)
#include "tst_database_worker.moc"
