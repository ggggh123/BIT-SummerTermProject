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
    void unknownNonemptyTokenCannotUseAnonymousLogin()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        QCOMPARE(ev::protocol::parseResponse(exchange(socket,"invalid-login","auth.user_login","unknown-session",
            {{"mobile","13800138000"}})).code,QString("AUTH_REQUIRED"));
        QCOMPARE(ev::protocol::parseResponse(exchange(socket,"invalid-admin-login","admin.login","unknown-session",
            {{"username","admin"},{"password","123456"}})).code,QString("AUTH_REQUIRED"));
        QVERIFY(ev::protocol::parseResponse(exchange(socket,"unknown-health","system.health","unknown-session")).ok);
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
    void restartAckThenShutdownCompletesExactlyOnce()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto admin = login(socket,true); QVERIFY(!admin.isEmpty());
        const auto ack = exchange(socket,"shutdown-restart","admin.charger_restart",admin,{{"chargerId",10}});
        QVERIFY(ev::protocol::parseResponse(ack).ok);
        context.shutdown(); // 成功 ACK 后立即关闭，stop 必须等已提交的重启完成。
        auto db = QSqlDatabase::addDatabase("QSQLITE","restart-shutdown-check");
        db.setDatabaseName(context.databasePath()); QVERIFY(db.open());
        {
            QSqlQuery query(db);
            QVERIFY(query.exec("SELECT status FROM chargers WHERE id=10")); QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(),QString("idle"));
            QVERIFY(query.exec("SELECT COUNT(*) FROM events WHERE event_type='admin.charger_restart.done' AND entity_id=10"));
            QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(),1);
            query.prepare("SELECT response_json FROM request_log WHERE request_id='shutdown-restart'");
            QVERIFY(query.exec()); QVERIFY(query.next()); QCOMPARE(query.value(0).toString().toUtf8(),ack);
        }
        db.close(); db = {}; QSqlDatabase::removeDatabase("restart-shutdown-check");
    }
    void failedFirstRestartAckNeverSchedulesCompletion()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto admin = login(socket,true); QVERIFY(!admin.isEmpty());
        auto db = QSqlDatabase::addDatabase("QSQLITE","restart-failure-check");
        db.setDatabaseName(context.databasePath()); QVERIFY(db.open());
        {
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER reject_restart_ack BEFORE INSERT ON request_log "
                "WHEN NEW.action='admin.charger_restart' BEGIN SELECT RAISE(ABORT,'test ACK failure'); END"));
            const auto ack = exchange(socket,"failed-restart","admin.charger_restart",admin,{{"chargerId",10}});
            QCOMPARE(ev::protocol::parseResponse(ack).code,QString("INTERNAL_ERROR"));
            QVERIFY(query.exec("DROP TRIGGER reject_restart_ack"));
            QTest::qWait(1700); // 单次跨过 1500 ms 完成时限，观察失败 ACK 没有延迟写。
            QVERIFY(query.exec("SELECT status FROM chargers WHERE id=10")); QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(),QString("fault"));
            QVERIFY(query.exec("SELECT COUNT(*) FROM events WHERE event_type IN ('admin.charger_restart','admin.charger_restart.done') AND entity_id=10"));
            QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(),0);
            QVERIFY(query.exec("SELECT COUNT(*) FROM request_log WHERE request_id='failed-restart'"));
            QVERIFY(query.next()); QCOMPARE(query.value(0).toInt(),0);
            QVERIFY(query.exec("SELECT version FROM snapshot_meta")); QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(),0);
        }
        db.close(); db = {}; QSqlDatabase::removeDatabase("restart-failure-check");
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
        const auto admin = login(socket,true);
        QVERIFY(!token.isEmpty() && !admin.isEmpty());
        for (int i = 0; i < 256; ++i)
            context.executeLocal({1,QString("fill-%1").arg(i),"user.get",token,{}},this,[](auto) {});
        QByteArray unknown,busy,health;
        const QList<ev::protocol::RequestEnvelope> rejected = {
            {1,"anonymous-full","demo.reset",{},{{"confirmation","RESET_DEMO"}}},
            {1,"user-full","demo.reset",token,{{"confirmation","RESET_DEMO"}}},
            {1,"confirmation-full","demo.reset",admin,{}},
            {1,"confirmation-type-full","demo.reset",admin,{{"confirmation",true}}},
            {1,"confirmation-case-full","demo.reset",admin,{{"confirmation","reset_demo"}}},
            {1,"sim-full","demo.reset","sim-token",{}},
            {1,"invalid-token-full","demo.reset","invalid-session",{}},
            {1,"amount-full","wallet.recharge",token,{{"amountFen",0}}},
            {1,"id-full","station.detail",token,{{"stationId",1.5}}},
            {1,"phone-full","auth.user_login",{},{{"mobile","bad"}}}
        };
        QStringList codes;
        for (const auto &request : rejected)
            context.executeLocal(request,this,[&](auto bytes) { codes.append(ev::protocol::parseResponse(bytes).code); });
        context.executeLocal({1,"unknown-full","nonexistent",token,{}},this,[&](auto bytes) { unknown = bytes; });
        context.executeLocal({1,"busy-full","user.get",token,{}},this,[&](auto bytes) { busy = bytes; });
        context.executeLocal({1,"health-full","system.health",{},{}},this,[&](auto bytes) { health = bytes; });
        QTRY_VERIFY(!unknown.isEmpty() && !busy.isEmpty() && !health.isEmpty());
        QCOMPARE(ev::protocol::parseResponse(unknown).code,QString("INVALID_REQUEST"));
        QCOMPARE(ev::protocol::parseResponse(busy).code,QString("SERVER_BUSY"));
        QVERIFY(ev::protocol::parseResponse(health).ok);
        QTRY_COMPARE(codes.size(),rejected.size());
        QCOMPARE(codes,(QStringList{"AUTH_REQUIRED","FORBIDDEN","INVALID_REQUEST","INVALID_REQUEST",
            "INVALID_REQUEST","FORBIDDEN","AUTH_REQUIRED","INVALID_REQUEST","INVALID_REQUEST","INVALID_PHONE"}));
    }
    void capacityRejectionDefersBusinessChecksWithoutAcceptingMutation()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto user=login(socket); const auto admin=login(socket,true);
        QVERIFY(!user.isEmpty() && !admin.isEmpty());
        struct Case { QString id,action,token; QJsonObject payload; QString admittedCode; };
        const QList<Case> cases={
            {"deferred-station","station.detail",user,{{"stationId",999999}},"ENTITY_NOT_FOUND"},
            {"deferred-restart","admin.charger_restart",admin,{{"chargerId",1}},"ORDER_STATE_CONFLICT"},
            {"deferred-forecast","forecast.publish","ml-token",{{"runId","deferred-run"},{"modelVersion","test"},
                {"generatedAt","2026-09-06T00:00:00+08:00"},{"dataCutoff","2026-09-06T00:00:00+08:00"},
                {"records",QJsonArray{}}},"FORECAST_INVALID"},
            {"deferred-recharge","wallet.recharge",user,{{"amountFen",100}},"OK"}
        };
        int drained=0;
        QStringList codes;
        QObject replies;
        for (int i=0;i<256;++i)
            context.executeLocal({1,QString("deferred-fill-%1").arg(i),"user.get",user,{}},&replies,[&](auto) { ++drained; });
        for (const auto &item : cases)
            context.executeLocal({1,item.id,item.action,item.token,item.payload},&replies,
                [&](auto bytes) { codes.append(ev::protocol::parseResponse(bytes).code); });
        QTRY_COMPARE(codes.size(),cases.size());
        QCOMPARE(codes,(QStringList{"SERVER_BUSY","SERVER_BUSY","SERVER_BUSY","SERVER_BUSY"}));
        QTRY_COMPARE(drained,256);
        const auto balanceBefore=ev::protocol::parseResponse(exchange(socket,"before-retry","user.get",user));
        QCOMPARE(balanceBefore.data.toObject().value("user").toObject().value("balanceFen").toInt(),50000);
        for (const auto &item : cases)
            QCOMPARE(ev::protocol::parseResponse(exchange(socket,item.id,item.action,item.token,item.payload)).code,item.admittedCode);
        const auto retry=exchange(socket,"deferred-recharge","wallet.recharge",user,{{"amountFen",100}});
        QCOMPARE(ev::protocol::parseResponse(retry).data.toObject().value("balanceFen").toInt(),50100);
        const auto balanceAfter=ev::protocol::parseResponse(exchange(socket,"after-retry","user.get",user));
        QCOMPARE(balanceAfter.data.toObject().value("user").toObject().value("balanceFen").toInt(),50100);
        drained=0;
        QByteArray busyReplay;
        QObject replayReplies;
        for (int i=0;i<256;++i)
            context.executeLocal({1,QString("replay-fill-%1").arg(i),"user.get",user,{}},&replayReplies,[&](auto) { ++drained; });
        context.executeLocal({1,"deferred-recharge","wallet.recharge",user,{{"amountFen",100}}},&replayReplies,
            [&](auto bytes) { busyReplay=bytes; });
        QTRY_VERIFY(!busyReplay.isEmpty());
        QCOMPARE(ev::protocol::parseResponse(busyReplay).code,QString("SERVER_BUSY"));
        QTRY_COMPARE(drained,256);
        QCOMPARE(exchange(socket,"deferred-recharge","wallet.recharge",user,{{"amountFen",100}}),retry);
    }
    void purePayloadErrorsMatchBeforeAndAfterCapacityAdmission()
    {
        QTemporaryDir dir; AppContext context; QVERIFY(initialize(context,dir).ok);
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,context.port()); QVERIFY(socket.waitForConnected());
        const auto user=login(socket); const auto admin=login(socket,true);
        QVERIFY(!user.isEmpty() && !admin.isEmpty());
        struct Case { QString action,token; QJsonObject payload; };
        const QList<Case> cases={
            {"auth.user_login",{},{{"mobile",123}}},
            {"admin.login",{},{{"username","admin"},{"password",false}}},
            {"user.update",user,{{"nickname"," "}}},
            {"wallet.recharge",user,{{"amountFen",9007199254740992.0}}},
            {"station.list",user,{{"latitude",true},{"longitude",10}}},
            {"charger.list",user,{{"stationId",0}}},
            {"charge.reserve",user,{{"chargerId","1"}}},
            {"charge.start",user,{}}, {"charge.stop",user,{}},
            {"charge.settle",user,{}}, {"order.cancel",user,{}},
            {"order.list",user,{{"limit",2.5}}},
            {"admin.dashboard",admin,{{"rangeDays",7.5}}},
            {"admin.station_create",admin,{{"name","test"},{"address","test"},{"longitude",120},
                {"priceFenPerKwh",100},{"fastChargerCount",1},{"slowChargerCount",0}}},
            {"admin.charger_restart",admin,{{"chargerId",-1}}},
            {"admin.user_list",admin,{{"mobileLike",true}}},
            {"admin.user_set_status",admin,{{"userId",1},{"status","unknown"}}},
            {"telemetry.push","sim-token",{{"chargerId",1},{"recordedAt","2026-09-06T10:00:00Z"},
                {"powerKw",1},{"energyIncrementKwh",0},{"status","idle"}}},
            {"simulator.fault_set","sim-token",{{"chargerId",1},{"recordedAt","2026-09-06T10:00:00+08:00"},{"fault",1}}},
            {"simulator.status","sim-token",{{"simulatedAt","2026-09-06T10:00:00+08:00"},{"state","running"},{"eventCount",0.5}}},
            {"forecast.publish","ml-token",{{"runId","test"},{"modelVersion","test"},{"generatedAt","2026-09-06T10:00:00+08:00"},
                {"dataCutoff","2026-09-06T10:00:00+08:00"},{"records",false}}}
        };
        int drained=0;
        QStringList codes;
        QObject replies; // 早退时丢弃全部按引用捕获的异步响应。
        for (int i=0;i<256;++i)
            context.executeLocal({1,QString("matrix-fill-%1").arg(i),"user.get",user,{}},&replies,[&](auto) { ++drained; });
        for (int i=0;i<cases.size();++i) {
            const auto &item=cases[i];
            context.executeLocal({1,QString("matrix-full-%1").arg(i),item.action,item.token,item.payload},&replies,
                [&](auto bytes) { codes.append(ev::protocol::parseResponse(bytes).code); });
        }
        QTRY_COMPARE(codes.size(),cases.size());
        for (int i=0;i<cases.size();++i) QCOMPARE(codes[i],QString("INVALID_REQUEST"));
        QTRY_COMPARE(drained,256);
        for (int i=0;i<cases.size();++i) {
            const auto &item=cases[i];
            QCOMPARE(ev::protocol::parseResponse(exchange(socket,QString("matrix-normal-%1").arg(i),
                item.action,item.token,item.payload)).code,QString("INVALID_REQUEST"));
        }
    }
};
QTEST_GUILESS_MAIN(ServerThreadsTest)
#include "tst_server_threads.moc"
