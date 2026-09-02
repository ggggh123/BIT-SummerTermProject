#include "contracts/Actions.h"
#include "contracts/Permissions.h"
#include "contracts/Statuses.h"

#include <QSet>
#include <QtTest/QtTest>

namespace {

QStringList expectedActions()
{
    return {
        QStringLiteral("auth.user_login"),
        QStringLiteral("user.get"),
        QStringLiteral("user.update"),
        QStringLiteral("wallet.recharge"),
        QStringLiteral("station.list"),
        QStringLiteral("station.detail"),
        QStringLiteral("charger.list"),
        QStringLiteral("charge.reserve"),
        QStringLiteral("charge.start"),
        QStringLiteral("charge.stop"),
        QStringLiteral("charge.settle"),
        QStringLiteral("order.current"),
        QStringLiteral("order.list"),
        QStringLiteral("order.cancel"),
        QStringLiteral("admin.login"),
        QStringLiteral("admin.dashboard"),
        QStringLiteral("admin.station_create"),
        QStringLiteral("admin.charger_restart"),
        QStringLiteral("admin.user_list"),
        QStringLiteral("admin.user_set_status"),
        QStringLiteral("telemetry.push"),
        QStringLiteral("simulator.fault_set"),
        QStringLiteral("simulator.status"),
        QStringLiteral("forecast.publish"),
        QStringLiteral("forecast.latest"),
        QStringLiteral("system.health"),
        QStringLiteral("demo.reset")
    };
}

} // namespace

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
    const QStringList expected = expectedActions();
    const QStringList actual = ev::actions::all();

    QCOMPARE(actual, expected);
    QCOMPARE(actual.size(), 27);

    QSet<QString> unique;
    for (const QString &action : actual) {
        QVERIFY2(!action.trimmed().isEmpty(), "canonical action names must not be blank");
        QVERIFY2(!unique.contains(action), qPrintable(QStringLiteral("duplicate action: %1").arg(action)));
        unique.insert(action);
    }
    QCOMPARE(unique.size(), 27);
}

void ContractsTest::statusesValidate()
{
    const QStringList users = {QStringLiteral("active"), QStringLiteral("frozen")};
    const QStringList chargers = {
        QStringLiteral("idle"),
        QStringLiteral("reserved"),
        QStringLiteral("charging"),
        QStringLiteral("fault"),
        QStringLiteral("restarting")
    };
    const QStringList orders = {
        QStringLiteral("reserved"),
        QStringLiteral("charging"),
        QStringLiteral("completed"),
        QStringLiteral("cancelled")
    };
    const QStringList congestions = {
        QStringLiteral("low"),
        QStringLiteral("medium"),
        QStringLiteral("high")
    };
    const QStringList forecastRuns = {QStringLiteral("active"), QStringLiteral("superseded")};

    QCOMPARE(ev::status::Users, users);
    QCOMPARE(ev::status::Chargers, chargers);
    QCOMPARE(ev::status::Orders, orders);
    QCOMPARE(ev::status::Congestions, congestions);
    QCOMPARE(ev::status::ForecastRuns, forecastRuns);

    for (const QString &value : users) {
        QVERIFY(ev::status::isUser(value));
    }
    for (const QString &value : chargers) {
        QVERIFY(ev::status::isCharger(value));
    }
    for (const QString &value : orders) {
        QVERIFY(ev::status::isOrder(value));
    }
    for (const QString &value : congestions) {
        QVERIFY(ev::status::isCongestion(value));
    }
    for (const QString &value : forecastRuns) {
        QVERIFY(ev::status::isForecastRun(value));
    }

    QVERIFY(!ev::status::isUser(QStringLiteral("disabled")));
    QVERIFY(!ev::status::isCharger(QStringLiteral("online")));
    QVERIFY(!ev::status::isOrder(QStringLiteral("settled")));
    QVERIFY(!ev::status::isCongestion(QStringLiteral("critical")));
    QVERIFY(!ev::status::isForecastRun(QStringLiteral("draft")));
}

void ContractsTest::permissionsAreStable()
{
    struct PermissionCase {
        QString actor;
        QStringList allowed;
    };

    const QList<PermissionCase> cases = {
        {QStringLiteral(""),
         {QStringLiteral("auth.user_login"),
          QStringLiteral("admin.login"),
          QStringLiteral("system.health")}},
        {QStringLiteral("user"),
         {QStringLiteral("user.get"),
          QStringLiteral("user.update"),
          QStringLiteral("wallet.recharge"),
          QStringLiteral("station.list"),
          QStringLiteral("station.detail"),
          QStringLiteral("charger.list"),
          QStringLiteral("charge.reserve"),
          QStringLiteral("charge.start"),
          QStringLiteral("charge.stop"),
          QStringLiteral("charge.settle"),
          QStringLiteral("order.current"),
          QStringLiteral("order.list"),
          QStringLiteral("order.cancel"),
          QStringLiteral("forecast.latest"),
          QStringLiteral("system.health")}},
        {QStringLiteral("admin"),
         {QStringLiteral("station.list"),
          QStringLiteral("station.detail"),
          QStringLiteral("charger.list"),
          QStringLiteral("admin.dashboard"),
          QStringLiteral("admin.station_create"),
          QStringLiteral("admin.charger_restart"),
          QStringLiteral("admin.user_list"),
          QStringLiteral("admin.user_set_status"),
          QStringLiteral("forecast.latest"),
          QStringLiteral("system.health"),
          QStringLiteral("demo.reset")}},
        {QStringLiteral("simulator"),
         {QStringLiteral("telemetry.push"),
          QStringLiteral("simulator.fault_set"),
          QStringLiteral("simulator.status"),
          QStringLiteral("system.health")}},
        {QStringLiteral("ml"),
         {QStringLiteral("forecast.publish"), QStringLiteral("system.health")}},
        {QStringLiteral("auditor"), {QStringLiteral("system.health")}}
    };

    const QStringList actions = expectedActions();
    for (const PermissionCase &permissionCase : cases) {
        const QSet<QString> allowed(permissionCase.allowed.cbegin(), permissionCase.allowed.cend());
        for (const QString &action : actions) {
            const bool expected = allowed.contains(action);
            const QByteArray message = QStringLiteral("actor='%1', action='%2'")
                                           .arg(permissionCase.actor, action)
                                           .toUtf8();
            QVERIFY2(ev::permissions::allows(permissionCase.actor, action) == expected, message.constData());
        }

        QVERIFY2(!ev::permissions::allows(permissionCase.actor, QStringLiteral("unknown.action")),
                 qPrintable(QStringLiteral("unknown action allowed for actor '%1'").arg(permissionCase.actor)));
        QVERIFY2(!ev::permissions::allows(permissionCase.actor, QStringLiteral("")),
                 qPrintable(QStringLiteral("blank action allowed for actor '%1'").arg(permissionCase.actor)));
    }
}

QTEST_MAIN(ContractsTest)

#include "tst_contracts.moc"
