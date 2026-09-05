#include "contracts/Actions.h"
#include "contracts/BusinessRules.h"
#include "contracts/Permissions.h"
#include "contracts/ResetReceipt.h"
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
    void businessFailuresFollowActionOrder();
    void pendingResetReceiptResumesAfterCrash();
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

void ContractsTest::businessFailuresFollowActionOrder()
{
    using ev::business::ChargeReserveFacts;
    using ev::business::DeviceEventFacts;

    QCOMPARE(ev::business::chargeReserveFailure({true, true, false}),
             QStringLiteral("USER_FROZEN"));
    QCOMPARE(ev::business::chargeReserveFailure({false, true, false}),
             QStringLiteral("ACTIVE_ORDER_EXISTS"));
    QCOMPARE(ev::business::chargeReserveFailure({false, false, false}),
             QStringLiteral("CHARGER_NOT_AVAILABLE"));
    QCOMPARE(ev::business::chargeReserveFailure({false, false, true}), QString());

    const DeviceEventFacts defaults;
    QVERIFY(!defaults.chargerExists);
    QVERIFY(!defaults.temporalConflict);
    QVERIFY(!defaults.stateTransitionConflict);

    QCOMPARE(ev::business::deviceEventFailure({false, true, true}),
             QStringLiteral("CHARGER_NOT_AVAILABLE"));
    QCOMPARE(ev::business::deviceEventFailure({true, true, false}),
             QStringLiteral("ORDER_STATE_CONFLICT"));
    QCOMPARE(ev::business::deviceEventFailure({true, false, true}),
             QStringLiteral("ORDER_STATE_CONFLICT"));
    QCOMPARE(ev::business::deviceEventFailure({true, false, false}), QString());
}

void ContractsTest::pendingResetReceiptResumesAfterCrash()
{
    using ev::reset::NextStep;

    const std::optional<ev::reset::Receipt> absent;
    QVERIFY(ev::reset::nextStep(absent) == NextStep::BeginCoreReset);

    const ev::reset::Receipt pending = ev::reset::makePendingReceipt(
        QStringLiteral("reset-request-7"),
        QStringLiteral("2026-09-02T10:30:00+08:00"),
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
        42);
    QVERIFY(ev::reset::nextStep(pending) == NextStep::ResumeSnapshot);

    const QByteArray ack = QByteArrayLiteral(
        R"({"requestId":"reset-request-7","ok":true,"code":"OK","message":"snapshot pending","data":{"resetAt":"2026-09-02T10:30:00+08:00","goldenHash":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}})");
    const ev::reset::Receipt final = ev::reset::finalizeReceipt(pending, ack);

    QCOMPARE(final.requestId, pending.requestId);
    QCOMPARE(final.resetAt, pending.resetAt);
    QCOMPARE(final.goldenHash, pending.goldenHash);
    QCOMPARE(final.snapshotVersion, pending.snapshotVersion);
    QCOMPARE(final.finalAck, ack);
    QVERIFY(ev::reset::nextStep(final) == NextStep::ReplayFinalAck);

    const QByteArray replacementAck = QByteArrayLiteral(
        R"({"requestId":"reset-request-7","ok":false,"code":"INTERNAL_ERROR","message":"must not replace final ACK","data":{}})");
    const ev::reset::Receipt finalizedAgain = ev::reset::finalizeReceipt(final, replacementAck);
    QCOMPARE(finalizedAgain.state, ev::reset::ReceiptState::Final);
    QCOMPARE(finalizedAgain.requestId, final.requestId);
    QCOMPARE(finalizedAgain.resetAt, final.resetAt);
    QCOMPARE(finalizedAgain.goldenHash, final.goldenHash);
    QCOMPARE(finalizedAgain.snapshotVersion, final.snapshotVersion);
    QCOMPARE(finalizedAgain.finalAck, ack);
}

QTEST_MAIN(ContractsTest)

#include "tst_contracts.moc"
