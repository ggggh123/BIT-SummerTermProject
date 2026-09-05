#include <QtTest>

#include <cmath>

#include "core/TelemetryEngine.h"

using namespace ev::simulator;

class TelemetryEngineTest : public QObject
{
    Q_OBJECT
private slots:
    void determinismAndTimeAdvance();
    void zeroEnergyForNonCharging();
    void faultAndRecoveryIntents();
    void intentTimestampsStrictlyIncreasing();
};

static QDateTime t0()
{
    return QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"),
                                 Qt::ISODate);
}

void TelemetryEngineTest::determinismAndTimeAdvance()
{
    QList<ChargerSnapshot> chargers;
    ChargerSnapshot c;
    c.chargerId = 1001;
    c.status = QStringLiteral("charging");
    c.powerKw = 60.0;
    chargers.append(c);

    TelemetryEngine a(20260901, t0(), 3000);
    TelemetryEngine b(20260901, t0(), 3000);
    a.replaceChargers(chargers);
    b.replaceChargers(chargers);

    for (int i = 0; i < 3; ++i) {
        const QList<TelemetrySample> sa = a.tick();
        const QList<TelemetrySample> sb = b.tick();
        QCOMPARE(sa.size(), 1);
        QCOMPARE(sa[0].energyIncrementKwh, sb[0].energyIncrementKwh);
        QVERIFY(std::isfinite(sa[0].energyIncrementKwh));
        QVERIFY(sa[0].energyIncrementKwh > 0.0);
    }
    QCOMPARE(a.currentTime(), t0().addMSecs(9000));
    QCOMPARE(b.currentTime(), t0().addMSecs(9000));
}

void TelemetryEngineTest::zeroEnergyForNonCharging()
{
    QList<ChargerSnapshot> chargers;
    int id = 1001;
    for (const char *s : {"idle", "fault", "restarting"}) {
        ChargerSnapshot c;
        c.chargerId = id++;
        c.status = QString::fromLatin1(s);
        c.powerKw = 60.0;
        chargers.append(c);
    }

    TelemetryEngine e(20260901, t0(), 3000);
    e.replaceChargers(chargers);
    const QList<TelemetrySample> samples = e.tick();
    QCOMPARE(samples.size(), 3);
    for (const TelemetrySample &s : samples) {
        QCOMPARE(s.energyIncrementKwh, 0.0);
        QCOMPARE(s.powerKw, 0.0);
    }
}

void TelemetryEngineTest::faultAndRecoveryIntents()
{
    ChargerSnapshot c;
    c.chargerId = 1001;
    c.status = QStringLiteral("idle");
    c.powerKw = 60.0;

    TelemetryEngine e(20260901, t0(), 3000);
    e.replaceChargers({c});

    QVERIFY(e.requestFault(1001));
    QVERIFY(!e.requestFault(1001));  // already fault

    QList<FaultIntent> intents = e.takePendingIntents();
    QCOMPARE(intents.size(), 1);
    QCOMPARE(intents[0].chargerId, 1001);
    QCOMPARE(intents[0].fault, true);

    QVERIFY(e.requestRecovery(1001));
    intents = e.takePendingIntents();
    QCOMPARE(intents.size(), 1);
    QCOMPARE(intents[0].fault, false);

    QVERIFY(!e.requestFault(9999));
    QVERIFY(!e.requestRecovery(9999));
}

// R14 regression: recordedAt must increase strictly across fault/recovery
// intents and interleaved telemetry, even without any tick in between.
void TelemetryEngineTest::intentTimestampsStrictlyIncreasing()
{
    ChargerSnapshot c;
    c.chargerId = 1001;
    c.status = QStringLiteral("idle");
    c.powerKw = 60.0;

    TelemetryEngine e(20260901, t0(), 3000);
    e.replaceChargers({c});

    QVERIFY(e.requestFault(1001));
    QList<FaultIntent> first = e.takePendingIntents();
    QCOMPARE(first.size(), 1);
    QVERIFY(first[0].recordedAt > t0());

    QVERIFY(e.requestRecovery(1001));  // status stays fault until admin reset
    QList<FaultIntent> second = e.takePendingIntents();
    QCOMPARE(second.size(), 1);
    QVERIFY(second[0].recordedAt > first[0].recordedAt);

    const QList<TelemetrySample> samples = e.tick();
    QVERIFY(!samples.isEmpty());
    QVERIFY(samples.first().recordedAt > second[0].recordedAt);

    QVERIFY(e.requestRecovery(1001));
    QList<FaultIntent> third = e.takePendingIntents();
    QCOMPARE(third.size(), 1);
    QVERIFY(third[0].recordedAt > samples.first().recordedAt);
}

QTEST_APPLESS_MAIN(TelemetryEngineTest)
#include "tst_telemetryengine.moc"
