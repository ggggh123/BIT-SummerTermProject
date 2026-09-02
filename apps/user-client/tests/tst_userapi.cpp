#include "app/UserAppConfig.h"
#include "net/TcpJsonClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "services/UserApi.h"
#include "ui/LoginPage.h"
#include "ui/MainWindow.h"

#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <QtEndian>

namespace {

constexpr auto kMobile = "13800138000";
constexpr auto kTimestamp = "2026-09-01T08:30:45.123+08:00";

QJsonObject userObject(QString mobile = QString::fromLatin1(kMobile), QString status = QStringLiteral("active"))
{
    return {
        {QStringLiteral("userId"), 42},
        {QStringLiteral("mobile"), std::move(mobile)},
        {QStringLiteral("nickname"), QStringLiteral("测试用户")},
        {QStringLiteral("avatarPath"), QString()},
        {QStringLiteral("balanceFen"), 12345},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("registeredAt"), QString::fromLatin1(kTimestamp)},
    };
}

QJsonObject orderObject(QString status = QStringLiteral("charging"))
{
    const bool charging = status == QStringLiteral("charging");
    return {
        {QStringLiteral("orderId"), 99},
        {QStringLiteral("userId"), 42},
        {QStringLiteral("chargerId"), 7},
        {QStringLiteral("stationId"), 3},
        {QStringLiteral("stationName"), QStringLiteral("星火充电站")},
        {QStringLiteral("chargerCode"), QStringLiteral("A-07")},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("reservedAt"), QString::fromLatin1(kTimestamp)},
        {QStringLiteral("startedAt"), charging ? QJsonValue(QString::fromLatin1(kTimestamp)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("endedAt"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("energyKwh"), 12.5},
        {QStringLiteral("amountFen"), 2345},
        {QStringLiteral("elapsedSec"), 360},
    };
}

QByteArray responseFrame(const QString &requestId, bool ok, QString code, QString message, QJsonValue data)
{
    return ev::protocol::encodeFrame(ev::protocol::toJson({requestId, ok, std::move(code), std::move(message), std::move(data)}));
}

bool connectToFakeServer(TcpJsonClient &client, QTcpServer &server)
{
    bool accepted = server.hasPendingConnections();
    bool connected = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    const auto quitWhenReady = [&] {
        if (accepted && connected) {
            loop.quit();
        }
    };
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&] {
        accepted = true;
        quitWhenReady();
    });
    QObject::connect(&client, &TcpJsonClient::connectionChanged, &loop, [&](bool connectedNow) {
        connected = connectedNow;
        quitWhenReady();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    client.connectToServer();
    timeout.start(5'000);
    loop.exec();
    return accepted && connected;
}

ev::protocol::RequestEnvelope takeRequest(QTcpSocket *peer)
{
    QElapsedTimer timer;
    timer.start();
    while (peer->bytesAvailable() < 4 && timer.elapsed() < 5'000) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < 4) {
        return {};
    }
    const QByteArray header = peer->read(4);
    const quint32 length = qFromBigEndian<quint32>(header.constData());
    while (peer->bytesAvailable() < static_cast<qint64>(length) && timer.elapsed() < 5'000) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < static_cast<qint64>(length)) {
        return {};
    }
    return ev::protocol::parseRequest(peer->read(length));
}

void reply(QTcpSocket *peer, const QString &requestId, bool ok, const QString &code, const QString &message, const QJsonValue &data)
{
    const QByteArray frame = responseFrame(requestId, ok, code, message, data);
    QCOMPARE(peer->write(frame), qint64{frame.size()});
    QVERIFY(peer->flush());
}

UserAppConfig usableConfig(quint16 port)
{
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = port;
    config.tencentMapKey = QStringLiteral("not-required-for-login");
    return config;
}

} // namespace

class UserApiTest final : public QObject {
    Q_OBJECT

private slots:
    void loginSendsOnlyPhoneAndDecodesCompleteSession();
    void malformedLoginSuccessIsInvalidResponseAndServerErrorIsPreserved();
    void staleLoginResponseCannotReplaceNewerSession();
    void currentOrderUsesOwnedTokenAndRequiresActiveOrderShape();
    void loginPageDisablesWhilePendingAndShowsConnectionFailure();
    void currentOrderGuardRoutesChargingAndNullToStablePages();
};

void UserApiTest::loginSendsOnlyPhoneAndDecodesCompleteSession()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    UserApi api(&client);
    QSignalSpy success(&api, &UserApi::loginSucceeded);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("auth.user_login"));
    QCOMPARE(request.token, QString());
    const QJsonObject expectedPayload{{QStringLiteral("mobile"), QString::fromLatin1(kMobile)}};
    QCOMPARE(request.payload, expectedPayload);

    reply(peer, request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("opaque-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_COMPARE(success.size(), 1);
    const ev::user::User user = qvariant_cast<ev::user::User>(success.takeFirst().at(0));
    QCOMPARE(user.userId, qint64{42});
    QCOMPARE(user.mobile, QString::fromLatin1(kMobile));
    QCOMPARE(user.nickname, QStringLiteral("测试用户"));
    QCOMPARE(user.balanceFen, qint64{12345});
    QCOMPARE(user.status, QStringLiteral("active"));
    QVERIFY(api.sessionUser().has_value());
    QCOMPARE(api.sessionUser()->userId, qint64{42});
}

void UserApiTest::malformedLoginSuccessIsInvalidResponseAndServerErrorIsPreserved()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto malformedRequest = takeRequest(peer);
    QJsonObject incomplete = userObject();
    incomplete.remove(QStringLiteral("registeredAt"));
    reply(peer, malformedRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("opaque-token")}, {QStringLiteral("user"), incomplete}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto frozenRequest = takeRequest(peer);
    reply(peer, frozenRequest.requestId, false, QStringLiteral("USER_FROZEN"), QStringLiteral("冻结用户不能预约"), QJsonObject{});
    QTRY_COMPARE(failures.size(), 1);
    const ev::user::ApiError failure = qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0));
    QCOMPARE(failure.code, QStringLiteral("USER_FROZEN"));
    QCOMPARE(failure.message, QStringLiteral("冻结用户不能预约"));
}

void UserApiTest::staleLoginResponseCannotReplaceNewerSession()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy successes(&api, &UserApi::loginSucceeded);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto first = takeRequest(peer);
    api.loginByPhone(QStringLiteral("13900139000"));
    const auto second = takeRequest(peer);
    reply(peer, second.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("new-token")}, {QStringLiteral("user"), userObject(QStringLiteral("13900139000"))}});
    QTRY_COMPARE(successes.size(), 1);
    reply(peer, first.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("old-token")}, {QStringLiteral("user"), userObject()}});
    QTest::qWait(50);
    QCOMPARE(successes.size(), 1);
    QVERIFY(api.sessionUser().has_value());
    QCOMPARE(api.sessionUser()->mobile, QStringLiteral("13900139000"));
}

void UserApiTest::currentOrderUsesOwnedTokenAndRequiresActiveOrderShape()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy orders(&api, &UserApi::currentOrderLoaded);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("owned-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    api.loadCurrentOrder();
    const auto badOrder = takeRequest(peer);
    QCOMPARE(badOrder.action, QStringLiteral("order.current"));
    QCOMPARE(badOrder.token, QStringLiteral("owned-token"));
    QCOMPARE(badOrder.payload, QJsonObject{});
    reply(peer, badOrder.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("completed"))}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));

    api.loadCurrentOrder();
    const auto nullOrder = takeRequest(peer);
    reply(peer, nullOrder.requestId, true, QStringLiteral("OK"), QString(), QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_COMPARE(orders.size(), 1);
    QVERIFY(!qvariant_cast<ev::user::CurrentOrderResult>(orders.takeFirst().at(0)).order.has_value());
}

void UserApiTest::loginPageDisablesWhilePendingAndShowsConnectionFailure()
{
    LoginPage page;
    auto *phone = page.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *button = page.findChild<QPushButton *>(QStringLiteral("loginButton"));
    auto *banner = page.findChild<QLabel *>(QStringLiteral("connectionBanner"));
    QVERIFY(phone != nullptr);
    QVERIFY(button != nullptr);
    QVERIFY(banner != nullptr);
    phone->setText(QString::fromLatin1(kMobile));
    page.setPending(true);
    QVERIFY(!button->isEnabled());
    page.setConnectionAvailable(false);
    QCOMPARE(banner->text(), QStringLiteral("服务器连接不可用"));
}

void UserApiTest::currentOrderGuardRoutesChargingAndNullToStablePages()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const auto exerciseGuard = [&](const QJsonValue &order, const QString &expectedName) {
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        auto *login = window.findChild<LoginPage *>(QStringLiteral("loginPage"));
        QVERIFY(login != nullptr);
        auto *phone = login->findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        auto *button = login->findChild<QPushButton *>(QStringLiteral("loginButton"));
        QVERIFY(phone != nullptr);
        QVERIFY(button != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer != nullptr);
        phone->setText(QString::fromLatin1(kMobile));
        button->click();
        const auto loginRequest = takeRequest(peer);
        reply(peer, loginRequest.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("guard-token")}, {QStringLiteral("user"), userObject()}});
        const auto currentRequest = takeRequest(peer);
        QCOMPARE(currentRequest.action, QStringLiteral("order.current"));
        reply(peer, currentRequest.requestId, true, QStringLiteral("OK"), QString(), QJsonObject{{QStringLiteral("order"), order}});
        auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
        QVERIFY(pages != nullptr);
        QTRY_COMPARE(pages->currentWidget()->objectName(), expectedName);
    };

    exerciseGuard(orderObject(), QStringLiteral("chargePage"));
    exerciseGuard(QJsonValue(QJsonValue::Null), QStringLiteral("nearbyPage"));
}

QTEST_MAIN(UserApiTest)
#include "tst_userapi.moc"
