#include "contracts/Actions.h"
#include "contracts/Permissions.h"
#include "contracts/Statuses.h"

#include <QtTest/QtTest>

class ContractsTest : public QObject
{
    Q_OBJECT

private slots:
    void actionsAreStable();
    void statusesValidate();
    void permissionsAreStable();
};

void ContractsTest::actionsAreStable()
{
    QCOMPARE(ev::actions::ChargeReserve, QStringLiteral("charge.reserve"));
    QCOMPARE(ev::actions::AuthUserLogin, QStringLiteral("auth.user_login"));
    QCOMPARE(ev::actions::AdminDashboard, QStringLiteral("admin.dashboard"));
    QCOMPARE(ev::actions::ChargeSettle, QStringLiteral("charge.settle"));
    QCOMPARE(ev::actions::ForecastPublish, QStringLiteral("forecast.publish"));
    QCOMPARE(ev::actions::TelemetryPush, QStringLiteral("telemetry.push"));
}

void ContractsTest::statusesValidate()
{
    QVERIFY(ev::status::isCharger(QStringLiteral("idle")));
    QVERIFY(ev::status::isCharger(QStringLiteral("restarting")));
    QVERIFY(!ev::status::isCharger(QStringLiteral("online")));
    QVERIFY(ev::status::isOrder(QStringLiteral("reserved")));
    QVERIFY(!ev::status::isOrder(QStringLiteral("settled")));
    QVERIFY(ev::status::isForecastRun(QStringLiteral("active")));
    QVERIFY(!ev::status::isForecastRun(QStringLiteral("draft")));
}

void ContractsTest::permissionsAreStable()
{
    QVERIFY(ev::permissions::allows(QStringLiteral(""), QStringLiteral("system.health")));
    QVERIFY(ev::permissions::allows(QStringLiteral("user"), QStringLiteral("forecast.latest")));
    QVERIFY(ev::permissions::allows(QStringLiteral("admin"), QStringLiteral("demo.reset")));
    QVERIFY(ev::permissions::allows(QStringLiteral("simulator"), QStringLiteral("telemetry.push")));
    QVERIFY(ev::permissions::allows(QStringLiteral("ml"), QStringLiteral("forecast.publish")));
    QVERIFY(!ev::permissions::allows(QStringLiteral("user"), QStringLiteral("demo.reset")));
}

QTEST_MAIN(ContractsTest)

#include "tst_contracts.moc"
