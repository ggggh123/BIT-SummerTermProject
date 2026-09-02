#include "protocol/JsonEnvelope.h"

#include <QFile>
#include <QJsonDocument>
#include <QtTest/QtTest>

class JsonEnvelopeTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsRequest();
    void rejectsMissingAndWrongFields();
    void classifiesRequestVersionErrors();
    void validatesResponseStringFields_data();
    void validatesResponseStringFields();
    void roundTripsFixtures();
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

void JsonEnvelopeTest::classifiesRequestVersionErrors()
{
    try {
        ev::protocol::parseRequest(R"({"requestId":"x","action":"system.health","payload":{}})");
        QFAIL("Expected missing version to be rejected");
    } catch (const ev::protocol::EnvelopeError &error) {
        QCOMPARE(error.code(), QStringLiteral("INVALID_REQUEST"));
    }

    try {
        ev::protocol::parseRequest(R"({"version":"1","requestId":"x","action":"system.health","payload":{}})");
        QFAIL("Expected string version to be rejected");
    } catch (const ev::protocol::EnvelopeError &error) {
        QCOMPARE(error.code(), QStringLiteral("INVALID_REQUEST"));
    }

    try {
        ev::protocol::parseRequest(R"({"version":2,"requestId":"x","action":"system.health","payload":{}})");
        QFAIL("Expected unsupported numeric version to be rejected");
    } catch (const ev::protocol::EnvelopeError &error) {
        QCOMPARE(error.code(), QStringLiteral("UNSUPPORTED_VERSION"));
    }

    try {
        ev::protocol::parseRequest(R"({"version":1.5,"requestId":"x","action":"system.health","payload":{}})");
        QFAIL("Expected non-v1 numeric version to be rejected");
    } catch (const ev::protocol::EnvelopeError &error) {
        QCOMPARE(error.code(), QStringLiteral("UNSUPPORTED_VERSION"));
    }
}

void JsonEnvelopeTest::validatesResponseStringFields_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<bool>("isValid");

    QTest::newRow("blank strings") << QByteArray(R"({"requestId":"","ok":true,"code":"","message":"","data":null})") << true;
    QTest::newRow("missing request id") << QByteArray(R"({"ok":true,"code":"OK","message":"healthy","data":{}})") << false;
    QTest::newRow("missing code") << QByteArray(R"({"requestId":"x","ok":true,"message":"healthy","data":{}})") << false;
    QTest::newRow("missing message") << QByteArray(R"({"requestId":"x","ok":true,"code":"OK","data":{}})") << false;
    QTest::newRow("non-string request id") << QByteArray(R"({"requestId":1,"ok":true,"code":"OK","message":"healthy","data":{}})") << false;
    QTest::newRow("non-string code") << QByteArray(R"({"requestId":"x","ok":true,"code":1,"message":"healthy","data":{}})") << false;
    QTest::newRow("non-string message") << QByteArray(R"({"requestId":"x","ok":true,"code":"OK","message":1,"data":{}})") << false;
}

void JsonEnvelopeTest::validatesResponseStringFields()
{
    QFETCH(QByteArray, json);
    QFETCH(bool, isValid);

    try {
        const auto response = ev::protocol::parseResponse(json);
        if (!isValid) {
            QFAIL("Expected response string field to be rejected");
        }
        QCOMPARE(response.requestId, QStringLiteral(""));
        QCOMPARE(response.code, QStringLiteral(""));
        QCOMPARE(response.message, QStringLiteral(""));
    } catch (const ev::protocol::EnvelopeError &error) {
        if (isValid) {
            QFAIL(qPrintable(QStringLiteral("Unexpected response error: ") + error.message()));
        }
        QCOMPARE(error.code(), QStringLiteral("INVALID_REQUEST"));
    }
}

void JsonEnvelopeTest::roundTripsFixtures()
{
    QFile requestFile(QStringLiteral(TEST_FIXTURE_DIR) + QStringLiteral("/request-health.json"));
    QVERIFY2(requestFile.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("Cannot open fixture: ") + requestFile.fileName()));
    const QByteArray requestFixture = requestFile.readAll();
    const auto request = ev::protocol::parseRequest(requestFixture);
    QCOMPARE(QJsonDocument::fromJson(ev::protocol::toJson(request)).object(),
             QJsonDocument::fromJson(requestFixture).object());

    QFile responseFile(QStringLiteral(TEST_FIXTURE_DIR) + QStringLiteral("/response-health.json"));
    QVERIFY2(responseFile.open(QIODevice::ReadOnly),
             qPrintable(QStringLiteral("Cannot open fixture: ") + responseFile.fileName()));
    const QByteArray responseFixture = responseFile.readAll();
    const auto response = ev::protocol::parseResponse(responseFixture);
    QCOMPARE(QJsonDocument::fromJson(ev::protocol::toJson(response)).object(),
             QJsonDocument::fromJson(responseFixture).object());
}

QTEST_MAIN(JsonEnvelopeTest)

#include "tst_jsonenvelope.moc"
