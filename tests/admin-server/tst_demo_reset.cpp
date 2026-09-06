#include "app/AppContext.h"
#include "protocol/JsonEnvelope.h"
#include <QTest>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QSqlQuery>

class DemoResetTest : public QObject {
    Q_OBJECT
    QByteArray call(AppContext &app, QString action, QString token, QJsonObject payload, QString id) {
        QEventLoop loop;
        QByteArray bytes;
        app.executeLocal({1,id,action,token,payload}, &loop, [&](QByteArray result) { bytes=result; loop.quit(); });
        QTimer::singleShot(5000,&loop,&QEventLoop::quit);
        loop.exec();
        return bytes;
    }
private slots:
    void resetRestoresGoldenAndReplaysWithoutErasingNewWrites() {
        QTemporaryDir dir;
        AppContext app;
        AppContext::Options options;
        options.databasePath=dir.filePath("runtime.db"); options.snapshotPath=dir.filePath("snapshot.json"); options.port=0;
        QVERIFY(app.initialize(options).ok);
        const auto admin=ev::protocol::parseResponse(call(app,"admin.login",{},{{"username","admin"},{"password","123456"}},"login"));
        QVERIFY(admin.ok);
        const QString token=admin.data.toObject().value("token").toString();
        const auto first=call(app,"demo.reset",token,{{"confirmation","RESET_DEMO"}},"reset-one");
        const auto response=ev::protocol::parseResponse(first);
        QVERIFY2(response.ok,first.constData());
        QCOMPARE(response.data.toObject().value("goldenHash").toString(),QString("5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7"));
        const auto login=ev::protocol::parseResponse(call(app,"auth.user_login",{},{{"mobile","13800138000"}},"user-login"));
        const auto user=login.data.toObject().value("token").toString();
        const int balance=login.data.toObject().value("user").toObject().value("balanceFen").toInt();
        QVERIFY(ev::protocol::parseResponse(call(app,"wallet.recharge",user,{{"amountFen",100}},"topup")).ok);
        QCOMPARE(call(app,"demo.reset",token,{{"confirmation","RESET_DEMO"},{"ignored",42}},"reset-one"),first);
        const auto profile=ev::protocol::parseResponse(call(app,"user.get",user,{},"profile"));
        QCOMPARE(profile.data.toObject().value("user").toObject().value("balanceFen").toInt(),balance+100);
    }
};
QTEST_GUILESS_MAIN(DemoResetTest)
#include "tst_demo_reset.moc"
