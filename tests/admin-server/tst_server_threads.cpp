#include "app/AppContext.h"
#include "protocol/JsonEnvelope.h"
#include <QTest>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <memory>
#include <vector>

namespace {
QByteArray framed(const QString &id, const QString &action, const QString &token = {}, const QJsonObject &payload = {})
{
    return ev::protocol::encodeFrame(ev::protocol::toJson({1,id,action,token,payload}));
}
QByteArray receive(QTcpSocket &socket, int timeout = 4000)
{
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) {
        if (socket.bytesAvailable() >= 4) {
            const auto header = socket.peek(4);
            const auto length = (quint32(quint8(header[0])) << 24) | (quint32(quint8(header[1])) << 16)
                | (quint32(quint8(header[2])) << 8) | quint8(header[3]);
            if (socket.bytesAvailable() >= length + 4) { socket.read(4); return socket.read(length); }
        }
        QTest::qWait(5);
    }
    return {};
}
QByteArray exchange(QTcpSocket &socket, const QString &id, const QString &action,
                    const QString &token = {}, const QJsonObject &payload = {})
{
    socket.write(framed(id,action,token,payload)); socket.flush();
    return receive(socket);
}
QString login(QTcpSocket &socket, bool admin = false)
{
    const auto bytes = exchange(socket,QUuid::createUuid().toString(),admin ? "admin.login" : "auth.user_login",{},
        admin ? QJsonObject{{"username","admin"},{"password","123456"}} : QJsonObject{{"mobile","13800138000"}});
    return bytes.isEmpty() ? QString{} : ev::protocol::parseResponse(bytes).data.toObject().value("token").toString();
}
Result initialize(AppContext &context, QTemporaryDir &dir, const QString &name = "runtime")
{
    AppContext::Options options;
    options.port = 0;
    options.databasePath = dir.filePath(name + ".db");
    options.snapshotPath = dir.filePath(name + ".json");
    return context.initialize(options);
}
}
class ServerThreadsTest : public QObject {
    Q_OBJECT
private slots:
    void headlessTerminationClosesNormally()
    {
        QTemporaryDir dir;
        QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost,0));
        const auto port = probe.serverPort(); probe.close();
        QProcess server;
        server.start(QStringLiteral(EV_ADMIN_SERVER_PATH),
            {"--server","--port",QString::number(port),"--db",dir.filePath("cli.db"),"--snapshot",dir.filePath("snapshot.json")});
        QVERIFY(server.waitForStarted());
        QVERIFY(server.waitForReadyRead(5000));
        QVERIFY(server.readAllStandardOutput().contains("listening on"));
        server.terminate();
        QVERIFY(server.waitForFinished(5000));
        QCOMPARE(server.exitStatus(),QProcess::NormalExit);
        QCOMPARE(server.exitCode(),0);
    }

    void splitCoalescedAndInvalidFrames()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port());
        QVERIFY(socket.waitForConnected());
        const auto first = framed("split","system.health");
        socket.write(first.left(2)); socket.flush(); QTest::qWait(20);
        QCOMPARE(socket.bytesAvailable(),0);
        socket.write(first.mid(2) + framed("glued","system.health")); socket.flush();
        QCOMPARE(ev::protocol::parseResponse(receive(socket)).requestId,QString("split"));
        QCOMPARE(ev::protocol::parseResponse(receive(socket)).requestId,QString("glued"));
        socket.write(QByteArray(4,'\0')); socket.flush();
        QCOMPARE(ev::protocol::parseResponse(receive(socket)).code,QString("INVALID_REQUEST"));
        QTRY_COMPARE(socket.state(),QAbstractSocket::UnconnectedState);
    }
    void capacityRejectsSeventeenthConnection()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        std::vector<std::unique_ptr<QTcpSocket>> sockets;
        for (int i = 0; i < 16; ++i) {
            auto socket = std::make_unique<QTcpSocket>();
            socket->connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket->waitForConnected());
            QVERIFY(ev::protocol::parseResponse(exchange(*socket,QString::number(i),"system.health")).ok);
            sockets.push_back(std::move(socket));
        }
        QTcpSocket extra; extra.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(extra.waitForConnected());
        QCOMPARE(ev::protocol::parseResponse(receive(extra)).code,QString("SERVER_BUSY"));
        sockets.front()->disconnectFromHost(); QTest::qWait(50);
        QTcpSocket replacement; replacement.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(replacement.waitForConnected());
        QVERIFY(ev::protocol::parseResponse(exchange(replacement,"replacement","system.health")).ok);
    }
    void permissionsAndIndependentContexts()
    {
        const auto connectionsBefore = QSqlDatabase::connectionNames();
        QTemporaryDir dir;
        {
            AppContext first,second; QVERIFY(initialize(first,dir,"first").ok); QVERIFY(initialize(second,dir,"second").ok);
            QTcpSocket a,b; a.connectToHost(QHostAddress::LocalHost,first.port()); b.connectToHost(QHostAddress::LocalHost,second.port());
            QVERIFY(a.waitForConnected()); QVERIFY(b.waitForConnected());
            const auto user = login(a); QVERIFY(!user.isEmpty());
            const auto admin = login(a,true); QVERIFY(!admin.isEmpty());
            QCOMPARE(ev::protocol::parseResponse(exchange(a,"wrong-role","wallet.recharge",admin,{{"amountFen",100}})).code,QString("FORBIDDEN"));
            QCOMPARE(ev::protocol::parseResponse(exchange(a,"unknown","private.query",admin)).code,QString("INVALID_REQUEST"));
            QCOMPARE(ev::protocol::parseResponse(exchange(a,"missing","admin.dashboard",{},{{"rangeDays",7}})).code,QString("AUTH_REQUIRED"));
            QCOMPARE(ev::protocol::parseResponse(exchange(b,"foreign","user.get",user)).code,QString("AUTH_REQUIRED"));
            QVERIFY(ev::protocol::parseResponse(exchange(a,"own","user.get",user)).ok);
        }
        QCOMPARE(QSqlDatabase::connectionNames(),connectionsBefore);
    }
    void restartIsCommittedOnceAndCompletesInWorker()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto token = login(socket,true); QVERIFY(!token.isEmpty());
        QElapsedTimer elapsed; elapsed.start();
        const auto bytes = exchange(socket,"restart","admin.charger_restart",token,{{"chargerId",10}});
        QVERIFY(ev::protocol::parseResponse(bytes).ok);
        QCOMPARE(ev::protocol::parseResponse(bytes).data.toObject().value("charger").toObject().value("status").toString(),QString("restarting"));
        QCOMPARE(exchange(socket,"restart","admin.charger_restart",token,{{"chargerId",10}}),bytes);
        auto status = [&] {
            const auto data = ev::protocol::parseResponse(exchange(socket,QUuid::createUuid().toString(),"charger.list",token,{{"stationId",2}})).data.toObject();
            for (const auto &value : data.value("chargers").toArray())
                if (value.toObject().value("chargerId").toInt() == 10) return value.toObject().value("status").toString();
            return QString{};
        };
        QCOMPARE(status(),QString("restarting"));
        QTRY_COMPARE_WITH_TIMEOUT(status(),QString("idle"),4000);
        QVERIFY(elapsed.elapsed() >= 1400);
        QCOMPARE(exchange(socket,"restart","admin.charger_restart",token,{{"chargerId",10}}),bytes);
    }
    void shutdownDrainsAcceptedCommandsAndDiscardsDeadReceivers()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto token = login(socket); QVERIFY(!token.isEmpty());
        int callbacks = 0;
        auto *receiver = new QObject;
        context.executeLocal({1,"deleted","wallet.recharge",token,{{"amountFen",100}}},receiver,[&](auto) { ++callbacks; });
        delete receiver;
        QTest::qWait(100);
        QCOMPARE(callbacks,0);
        for (int i = 0; i < 8; ++i)
            context.executeLocal({1,QString("queued-%1").arg(i),"wallet.recharge",token,{{"amountFen",100}}},this,[&](auto) { ++callbacks; });
        context.shutdown();
        QCoreApplication::processEvents();
        QCOMPARE(callbacks,0);
        auto db = QSqlDatabase::addDatabase("QSQLITE","drain-check");
        db.setDatabaseName(context.databasePath()); QVERIFY(db.open());
        {
            QSqlQuery query(db); QVERIFY(query.exec("SELECT balance_fen FROM users WHERE mobile='13800138000'"));
            QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(),50900);
        }
        db.close(); db = {}; QSqlDatabase::removeDatabase("drain-check");
    }
    void disconnectedClientCanReplayItsAlreadyAcceptedCommand()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto token = login(socket); QVERIFY(!token.isEmpty());
        auto db = QSqlDatabase::addDatabase("QSQLITE","disconnect-lock");
        db.setDatabaseName(context.databasePath()); QVERIFY(db.open());
        {
            QSqlQuery lock(db); QVERIFY(lock.exec("BEGIN IMMEDIATE"));
            socket.write(framed("lost-reply","wallet.recharge",token,{{"amountFen",100}})); socket.flush();
            QTest::qWait(100);
            socket.abort();
            QVERIFY(lock.exec("ROLLBACK"));
            QTcpSocket reconnected;
            reconnected.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(reconnected.waitForConnected());
            const auto replay = exchange(reconnected,"lost-reply","wallet.recharge",token,{{"amountFen",100}});
            QVERIFY(ev::protocol::parseResponse(replay).ok);
            QCOMPARE(exchange(reconnected,"lost-reply","wallet.recharge",token,{{"amountFen",100}}),replay);
            const auto user = ev::protocol::parseResponse(exchange(reconnected,"balance","user.get",token)).data.toObject().value("user").toObject();
            QCOMPARE(user.value("balanceFen").toInt(),50100);
        }
        db.close(); db = {}; QSqlDatabase::removeDatabase("disconnect-lock");
    }

    void internalViewsRequireAdminAndAreNotTcpActions()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto user = login(socket); const auto admin = login(socket,true);
        QByteArray bytes;
        context.queryAdmin(AdminView::RequestLog,user,{},this,[&](auto reply) { bytes = reply; });
        QTRY_VERIFY(!bytes.isEmpty());
        QCOMPARE(ev::protocol::parseResponse(bytes).code,QString("FORBIDDEN"));
        bytes.clear();
        context.queryAdmin(AdminView::RequestLog,{}, {},this,[&](auto reply) { bytes = reply; });
        QTRY_VERIFY(!bytes.isEmpty());
        QCOMPARE(ev::protocol::parseResponse(bytes).code,QString("AUTH_REQUIRED"));
        bytes.clear();
        context.queryAdmin(AdminView::RequestLog,admin,{},this,[&](auto reply) { bytes = reply; });
        QTRY_VERIFY(!bytes.isEmpty());
        QVERIFY(ev::protocol::parseResponse(bytes).ok);
        QCOMPARE(ev::protocol::parseResponse(exchange(socket,"internal","admin.request_log",admin)).code,QString("INVALID_REQUEST"));
    }
    void queueCapacityDoesNotHideUnknownActionsOrCachedHealth()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto token = login(socket);
        for (int i = 0; i < 256; ++i)
            context.executeLocal({1,QString("fill-%1").arg(i),"user.get",token,{}},this,[](auto) {});
        QByteArray unknown,busy,health;
        context.executeLocal({1,"unknown-full","nonexistent",token,{}},this,[&](auto bytes) { unknown = bytes; });
        context.executeLocal({1,"busy-full","user.get",token,{}},this,[&](auto bytes) { busy = bytes; });
        context.executeLocal({1,"health-full","system.health",{},{}},this,[&](auto bytes) { health = bytes; });
        QTRY_VERIFY(!unknown.isEmpty() && !busy.isEmpty() && !health.isEmpty());
        QCOMPARE(ev::protocol::parseResponse(unknown).code,QString("INVALID_REQUEST"));
        QCOMPARE(ev::protocol::parseResponse(busy).code,QString("SERVER_BUSY"));
        QVERIFY(ev::protocol::parseResponse(health).ok);
    }
};
QTEST_GUILESS_MAIN(ServerThreadsTest)
#include "tst_server_threads.moc"
