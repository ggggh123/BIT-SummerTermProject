#include "protocol/JsonEnvelope.h"

#include <QFile>
#include <QtTest/QtTest>

class JsonEnvelopeTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsRequest();
    void rejectsMissingAndWrongFields();
    void roundTripsFixtures();

private:
    QByteArray readFixture(const QString &name) const;
};

void JsonEnvelopeTest::roundTripsRequest()
{
    ev::protocol::RequestEnvelope request{1, QStringLiteral("req-1"), QStringLiteral("system.health"), QStringLiteral(""), {}};
    const auto parsed = ev::protocol::parseRequest(ev::protocol::toJson(request));
    QCOMPARE(parsed.requestId, QStringLiteral("req-1"));
    QCOMPARE(parsed.action, QStringLiteral("system.health"));
}

void JsonEnvelopeTest::rejectsMissingAndWrongFields()
{
    QVERIFY_EXCEPTION_THROWN(ev::protocol::parseRequest(R"({"version":1})"),
                             ev::protocol::EnvelopeError);
    QVERIFY_EXCEPTION_THROWN(ev::protocol::parseRequest(
                                 R"({"version":2,"requestId":"x","action":"system.health","payload":{}})"),
                             ev::protocol::EnvelopeError);
}

void JsonEnvelopeTest::roundTripsFixtures()
{
    const QByteArray requestFixture = readFixture(QStringLiteral("request-health.json"));
    const auto request = ev::protocol::parseRequest(requestFixture);
    QCOMPARE(request.version, 1);
    QCOMPARE(request.requestId, QStringLiteral("fixture-health"));
    QCOMPARE(request.action, QStringLiteral("system.health"));
    QCOMPARE(request.token, QStringLiteral(""));
    QCOMPARE(request.payload, QJsonObject{});

    const QByteArray responseFixture = readFixture(QStringLiteral("response-health.json"));
    const auto response = ev::protocol::parseResponse(responseFixture);
    QCOMPARE(response.requestId, QStringLiteral("fixture-health"));
    QCOMPARE(response.ok, true);
    QCOMPARE(response.code, QStringLiteral("OK"));
    QCOMPARE(response.message, QStringLiteral("healthy"));
    QCOMPARE(response.data.toObject().value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
}

QByteArray JsonEnvelopeTest::readFixture(const QString &name) const
{
    QFile file(QStringLiteral(TEST_FIXTURE_DIR) + QStringLiteral("/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        QFAIL(qPrintable(QStringLiteral("Cannot open fixture: ") + file.fileName()));
    }
    return file.readAll();
}

QTEST_MAIN(JsonEnvelopeTest)

#include "tst_jsonenvelope.moc"
