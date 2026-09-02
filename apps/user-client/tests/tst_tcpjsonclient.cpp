#include "net/TcpJsonClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QtEndian>
#include <QtTest>

namespace {

ev::protocol::ResponseEnvelope successfulResponse(const QString &requestId, int value)
{
    return {requestId, true, QStringLiteral("OK"), QString(), QJsonObject{{QStringLiteral("value"), value}}};
}

QByteArray responseFrame(const QString &requestId, int value)
{
    return ev::protocol::encodeFrame(ev::protocol::toJson(successfulResponse(requestId, value)));
}

bool connectToFakeServer(TcpJsonClient &client, QTcpServer &server)
{
    bool accepted = server.hasPendingConnections();
    bool connected = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    const auto finishWhenReady = [&] {
        if (accepted && connected) {
            loop.quit();
        }
    };
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&] {
        accepted = true;
        finishWhenReady();
    });
    QObject::connect(&client, &TcpJsonClient::connectionChanged, &loop, [&](bool isConnected) {
        connected = isConnected;
        finishWhenReady();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    client.connectToServer();
    timeout.start(5'000);
    loop.exec();
    return accepted && connected;
}

} // namespace

class TcpJsonClientTest final : public QObject {
    Q_OBJECT

private slots:
    void requestUsesBigEndianFrameAndV1Envelope();
    void splitResponseEmitsOneResponse();
    void coalescedResponsesEmitTwoResponses();
    void disconnectMidFrameFailsMutationOnce();
    void oversizedHeaderFailsPendingRequestAsProtocolError();
};

void TcpJsonClientTest::requestUsesBigEndianFrameAndV1Envelope()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::AnyIPv4));

    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    const QString requestId = client.send(
        QStringLiteral("station.detail"), QJsonObject{{QStringLiteral("stationId"), QStringLiteral("s-1")}}, QStringLiteral("token-1"));
    QVERIFY(!requestId.isEmpty());
    QVERIFY(!requestId.contains(QLatin1Char('{')));
    QVERIFY(!requestId.contains(QLatin1Char('}')));

    QTRY_VERIFY(peer->bytesAvailable() >= 4);
    const QByteArray header = peer->read(4);
    QCOMPARE(header.size(), 4);
    const quint32 payloadLength = qFromBigEndian<quint32>(header.constData());
    QVERIFY(payloadLength > 0);
    QTRY_VERIFY(peer->bytesAvailable() >= static_cast<qint64>(payloadLength));

    const auto request = ev::protocol::parseRequest(peer->read(static_cast<qint64>(payloadLength)));
    QCOMPARE(request.version, 1);
    QCOMPARE(request.requestId, requestId);
    QCOMPARE(request.action, QStringLiteral("station.detail"));
    QCOMPARE(request.token, QStringLiteral("token-1"));
    QCOMPARE(request.payload.value(QStringLiteral("stationId")).toString(), QStringLiteral("s-1"));
}

void TcpJsonClientTest::splitResponseEmitsOneResponse()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::AnyIPv4));

    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();

    QList<ev::protocol::ResponseEnvelope> received;
    connect(&client, &TcpJsonClient::responseReceived, this, [&received](const ev::protocol::ResponseEnvelope &response) {
        received.append(response);
    });

    const QString requestId = client.send(QStringLiteral("system.health"), {});
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    peer->readAll();

    const QByteArray frame = responseFrame(requestId, 7);
    QCOMPARE(peer->write(frame.left(5)), qint64{5});
    QVERIFY(peer->flush());
    QTest::qWait(30);
    QCOMPARE(received.size(), 0);

    QCOMPARE(peer->write(frame.mid(5)), qint64{frame.size() - 5});
    QVERIFY(peer->flush());
    QTRY_COMPARE(received.size(), 1);
    QCOMPARE(received.front().requestId, requestId);
    QCOMPARE(received.front().data.toObject().value(QStringLiteral("value")).toInt(), 7);
}

void TcpJsonClientTest::coalescedResponsesEmitTwoResponses()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::AnyIPv4));

    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();

    QList<ev::protocol::ResponseEnvelope> received;
    connect(&client, &TcpJsonClient::responseReceived, this, [&received](const ev::protocol::ResponseEnvelope &response) {
        received.append(response);
    });

    const QString firstId = client.send(QStringLiteral("system.health"), {});
    const QString secondId = client.send(QStringLiteral("station.list"), {});
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    peer->readAll();

    const QByteArray coalesced = responseFrame(firstId, 1) + responseFrame(secondId, 2);
    QCOMPARE(peer->write(coalesced), qint64{coalesced.size()});
    QVERIFY(peer->flush());

    QTRY_COMPARE(received.size(), 2);
    QCOMPARE(received.at(0).requestId, firstId);
    QCOMPARE(received.at(1).requestId, secondId);
}

void TcpJsonClientTest::disconnectMidFrameFailsMutationOnce()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::AnyIPv4));

    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();

    struct Failure { QString requestId; QString code; QString message; };
    QList<Failure> failures;
    connect(&client, &TcpJsonClient::transportFailed, this, [&failures](QString requestId, QString code, QString message) {
        failures.append({std::move(requestId), std::move(code), std::move(message)});
    });

    const QString requestId = client.send(QStringLiteral("order.create"), QJsonObject{{QStringLiteral("chargerId"), QStringLiteral("c-1")}});
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    peer->readAll();

    const QByteArray frame = responseFrame(requestId, 1);
    QCOMPARE(peer->write(frame.left(6)), qint64{6});
    QVERIFY(peer->flush());
    peer->disconnectFromHost();

    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(failures.front().requestId, requestId);
    QCOMPARE(failures.front().code, QStringLiteral("TRANSPORT_ERROR"));
    QTest::qWait(50);
    QCOMPARE(failures.size(), 1);
}

void TcpJsonClientTest::oversizedHeaderFailsPendingRequestAsProtocolError()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::AnyIPv4));

    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();

    struct Failure { QString requestId; QString code; QString message; };
    QList<Failure> failures;
    connect(&client, &TcpJsonClient::transportFailed, this, [&failures](QString requestId, QString code, QString message) {
        failures.append({std::move(requestId), std::move(code), std::move(message)});
    });

    const QString requestId = client.send(QStringLiteral("station.detail"), QJsonObject{{QStringLiteral("stationId"), QStringLiteral("s-1")}});
    QTRY_VERIFY(peer->bytesAvailable() > 0);
    peer->readAll();

    QByteArray oversizedHeader(4, Qt::Uninitialized);
    qToBigEndian<quint32>(ev::protocol::MaxPayloadBytes + 1U, oversizedHeader.data());
    QCOMPARE(peer->write(oversizedHeader), qint64{4});
    QVERIFY(peer->flush());

    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(failures.front().requestId, requestId);
    QCOMPARE(failures.front().code, QStringLiteral("PROTOCOL_ERROR"));
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    TcpJsonClientTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_tcpjsonclient.moc"
