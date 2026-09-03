#include "app/WebEngineRuntime.h"
#include "domain/Models.h"
#include "net/TencentMapClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "services/UserApi.h"
#include "ui/MainWindow.h"
#include "ui/NavigationPage.h"
#include "ui/NearbyPage.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QVariant>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QtTest>
#include <QtEndian>

#include <cstring>
#include <memory>
#include <utility>

namespace {

struct ReplyPlan final {
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    bool neverFinish = false;
};

class FakeReply final : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest &request, ReplyPlan plan, QObject *parent)
        : QNetworkReply(parent)
        , body_(std::move(plan.body))
        , plannedError_(plan.error)
        , neverFinish_(plan.neverFinish)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!neverFinish_) {
            QTimer::singleShot(0, this, [this] {
                if (plannedError_ != QNetworkReply::NoError) {
                    setError(plannedError_, QStringLiteral("network diagnostic must be redacted"));
                    emit errorOccurred(plannedError_);
                } else {
                    emit readyRead();
                }
                setFinished(true);
                emit finished();
            });
        }
    }

    void abort() override
    {
        if (isFinished()) {
            return;
        }
        setFinished(true);
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        emit errorOccurred(QNetworkReply::OperationCanceledError);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return body_.size() - offset_ + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (offset_ >= body_.size()) {
            return -1;
        }
        const qint64 count = std::min(maxSize, body_.size() - offset_);
        std::memcpy(data, body_.constData() + offset_, static_cast<size_t>(count));
        offset_ += count;
        return count;
    }

private:
    QByteArray body_;
    QNetworkReply::NetworkError plannedError_;
    bool neverFinish_;
    qint64 offset_ = 0;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager {
public:
    QList<ReplyPlan> plans;
    QList<QNetworkRequest> requests;
    QPointer<QNetworkReply> lastReply;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        Q_UNUSED(outgoingData)
        Q_ASSERT(operation == GetOperation);
        requests.append(request);
        const ReplyPlan plan = plans.isEmpty() ? ReplyPlan{} : plans.takeFirst();
        auto *reply = new FakeReply(request, plan, this);
        lastReply = reply;
        return reply;
    }
};

ev::user::Station station(qint64 stationId, bool forecastEnabled, double distanceKm)
{
    ev::user::Station value;
    value.stationId = stationId;
    value.name = QStringLiteral("测试站%1").arg(stationId);
    value.address = QStringLiteral("北京市海淀区测试地址%1号").arg(stationId);
    value.latitude = 39.95;
    value.longitude = 116.32;
    value.priceFenPerKwh = 135;
    value.forecastEnabled = forecastEnabled;
    value.chargerCount = 4;
    value.idleCount = 2;
    value.distanceKm = distanceKm;
    return value;
}

ev::user::Charger charger(qint64 chargerId, qint64 stationId,
                          QString status = QStringLiteral("idle"))
{
    ev::user::Charger value;
    value.chargerId = chargerId;
    value.stationId = stationId;
    value.code = QStringLiteral("C-%1").arg(chargerId);
    value.type = QStringLiteral("fast");
    value.powerKw = 60.0;
    value.status = std::move(status);
    value.chargeCount = 1;
    value.totalDurationSec = 120;
    value.updatedAt = QStringLiteral("2026-09-01T08:30:45+08:00");
    return value;
}

QVariant runJavaScriptAndWait(QWebEnginePage *page, const QString &script, bool *completed)
{
    struct Result final {
        QVariant value;
        bool completed = false;
    };
    const auto result = std::make_shared<Result>();
    QEventLoop loop;
    const QPointer<QEventLoop> guardedLoop(&loop);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    page->runJavaScript(script, [result, guardedLoop](const QVariant &value) {
        result->value = value;
        result->completed = true;
        if (guardedLoop != nullptr) {
            guardedLoop->quit();
        }
    });
    timeout.start(5'000);
    loop.exec();
    *completed = result->completed;
    return result->value;
}

QString jsonString(const QString &value)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

QByteArray responseFrame(const QString &requestId, bool ok, QString code,
                         QString message, QJsonValue data)
{
    return ev::protocol::encodeFrame(ev::protocol::toJson(
        {requestId, ok, std::move(code), std::move(message), std::move(data)}));
}

void reply(QTcpSocket *peer, const QString &requestId, bool ok, const QString &code,
           const QString &message, const QJsonValue &data)
{
    const QByteArray frame = responseFrame(requestId, ok, code, message, data);
    QCOMPARE(peer->write(frame), qint64{frame.size()});
    QVERIFY(peer->flush());
}

ev::protocol::RequestEnvelope takeRequest(QTcpSocket *peer, int timeoutMs = 5'000)
{
    QElapsedTimer timer;
    timer.start();
    while (peer->bytesAvailable() < 4 && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < 4) {
        return {};
    }
    const QByteArray header = peer->read(4);
    const quint32 length = qFromBigEndian<quint32>(header.constData());
    while (peer->bytesAvailable() < static_cast<qint64>(length)
           && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < static_cast<qint64>(length)) {
        return {};
    }
    return ev::protocol::parseRequest(peer->read(length));
}

QTcpSocket *waitForPeer(QTcpServer &server, int timeoutMs = 5'000)
{
    QElapsedTimer timer;
    timer.start();
    while (!server.hasPendingConnections() && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    return server.nextPendingConnection();
}

QJsonObject mainUserObject(qint64 userId = 42)
{
    return {
        {QStringLiteral("userId"), userId},
        {QStringLiteral("mobile"), QStringLiteral("13800138000")},
        {QStringLiteral("nickname"), QStringLiteral("导航测试用户%1").arg(userId)},
        {QStringLiteral("avatarPath"), QString()},
        {QStringLiteral("balanceFen"), 12'345},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("registeredAt"), QStringLiteral("2026-09-01T07:00:00+08:00")},
    };
}

void completeMainLogin(MainWindow &window, QTcpSocket *peer, qint64 userId = 42)
{
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    QVERIFY(phone != nullptr);
    QVERIFY(loginButton != nullptr);
    phone->setText(QStringLiteral("13800138000"));
    loginButton->click();
    auto request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("auth.user_login"));
    reply(peer, request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("navigation-session-%1").arg(userId)},
                      {QStringLiteral("user"), mainUserObject(userId)}});
    request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("order.current"));
    reply(peer, request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_VERIFY(!window.findChild<QWidget *>(QStringLiteral("authenticatedNavigation"))->isHidden());
}

QString installDeferredRouteHarness()
{
    return QStringLiteral(R"JS(
        (() => {
          const status = document.getElementById('route-status');
          window.__hardeningRoute = {active: '', deferred: false, pending: {}, overlay: false, resetCount: 0};
          window.renderRoute = (route, operationId) => {
            window.__hardeningRoute.active = operationId;
            const complete = () => {
              if (window.__hardeningRoute.active === operationId) {
                window.__hardeningRoute.overlay = true;
                status.textContent = `route-success:${route.stationName}`;
              }
            };
            if (!window.__hardeningRoute.deferred) {
              complete();
              return Promise.resolve();
            }
            return new Promise((resolve) => {
              window.__hardeningRoute.pending[operationId] = () => { complete(); resolve(); };
            });
          };
          window.invalidateRouteAttempt = (operationId) => {
            if (window.__hardeningRoute.active !== operationId) return false;
            window.__hardeningRoute.active = '';
            return true;
          };
          window.resetRouteSession = () => {
            window.__hardeningRoute.active = '';
            window.__hardeningRoute.pending = {};
            window.__hardeningRoute.overlay = false;
            window.__hardeningRoute.resetCount += 1;
            status.textContent = 'route-session-reset';
            return true;
          };
          return 'ready';
        })()
    )JS");
}

} // namespace

class TencentMapClientTest final : public QObject {
    Q_OBJECT

private slots:
    void requestBuilderUsesOfficialEndpointAndEncodedQueryOnly();
    void asyncGeocoderHandlesSuccessTencentNetworkMalformedAndTimeoutOnce();
    void destructionCancelsExternalManagerReplyWithoutLateSignals();
    void navigationScriptsAreJsonEscapedValidatedAndKeyFreeInCompletionState();
    void routeOperationCorrelationCachesOnlyMatchingSuccessAndRetainsLastSuccess();
    void realNavigationPageRunsQrcPromisePollingAndRetryOffline();
    void navigationPageDestructionIgnoresPendingWebCallbacks();
    void mainWindowTopNavigationInvalidatesHiddenRouteAndPreservesSuccess();
    void sessionExpiryClearsNavigationCacheBeforeRelogin();
    void resourceAndPageContractsRemainFixedAndDisplayPredictionStates();
    void nearbyChargerButtonsLocalizeEveryWireStatus();
    void nearbySelectionCarriesOriginStationAndChargerWithoutMutation();
};

void TencentMapClientTest::requestBuilderUsesOfficialEndpointAndEncodedQueryOnly()
{
    const QString key = QStringLiteral("unit map value +&=");
    const QString address = QStringLiteral("北京理工大学中关村校区 A&B");
    const QUrl url = TencentMapClient::buildGeocodeUrl(
        TencentMapClient::productionEndpoint(), address, key);
    QCOMPARE(url.adjusted(QUrl::RemoveQuery), QUrl(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/")));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("address")), address);
    QCOMPARE(query.queryItemValue(QStringLiteral("key")), key);
    QCOMPARE(query.queryItemValue(QStringLiteral("output")), QStringLiteral("json"));
    QCOMPARE(query.queryItems().size(), 3);
    const QByteArray encoded = url.toEncoded();
    QVERIFY(encoded.contains("address=%E5%8C%97%E4%BA%AC"));
    QVERIFY(encoded.contains("%26"));
    QVERIFY(!encoded.contains(address.toUtf8()));
}

void TencentMapClientTest::asyncGeocoderHandlesSuccessTencentNetworkMalformedAndTimeoutOnce()
{
    const QString key = QStringLiteral("private runtime sentinel +&=");
    FakeNetworkAccessManager manager;
    manager.plans = {
        {R"({"status":0,"message":"query ok","result":{"title":"ignored","location":{"lat":39.958,"lng":116.317}}})"},
        {R"({"status":347,"message":"request rejected"})"},
        {{}, QNetworkReply::ConnectionRefusedError},
        {R"({"status":0,"result":{"location":{"lat":"bad","lng":116.3}}})"},
        {{}, QNetworkReply::NoError, true},
    };
    TencentMapClient client(key, &manager, QUrl(QStringLiteral("https://offline.invalid/geocode")), 20);
    QSignalSpy succeeded(&client, &TencentMapClient::geocodeSucceeded);
    QSignalSpy failed(&client, &TencentMapClient::geocodeFailed);

    const QString successId = client.geocode(QStringLiteral("北京理工大学中关村校区"));
    QTRY_COMPARE(succeeded.size(), 1);
    const auto successArgs = succeeded.takeFirst();
    QCOMPARE(successArgs.at(0).toString(), successId);
    const auto point = qvariant_cast<ev::user::GeoPoint>(successArgs.at(1));
    QCOMPARE(point.latitude, 39.958);
    QCOMPARE(point.longitude, 116.317);

    const QString tencentFailureId = client.geocode(QStringLiteral("北京航空航天大学学院路校区"));
    QVERIFY(!tencentFailureId.isEmpty());
    QTRY_COMPARE(failed.size(), 1);
    const auto tencentError = qvariant_cast<ev::user::ApiError>(failed.takeFirst().at(0));
    QCOMPARE(tencentError.code, QStringLiteral("MAP_API_ERROR"));

    const QString networkFailureId = client.geocode(QStringLiteral("北京市海淀区西二旗"));
    QVERIFY(!networkFailureId.isEmpty());
    QTRY_COMPARE(failed.size(), 1);
    const auto networkError = qvariant_cast<ev::user::ApiError>(failed.takeFirst().at(0));
    QCOMPARE(networkError.code, QStringLiteral("NETWORK_ERROR"));

    const QString malformedId = client.geocode(QStringLiteral("格式错误地址"));
    QVERIFY(!malformedId.isEmpty());
    QTRY_COMPARE(failed.size(), 1);
    const auto malformedError = qvariant_cast<ev::user::ApiError>(failed.takeFirst().at(0));
    QCOMPARE(malformedError.code, QStringLiteral("INVALID_RESPONSE"));

    const QString timeoutId = client.geocode(QStringLiteral("超时地址"));
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 500);
    const auto timeoutError = qvariant_cast<ev::user::ApiError>(failed.takeFirst().at(0));
    QCOMPARE(timeoutError.requestId, timeoutId);
    QCOMPARE(timeoutError.code, QStringLiteral("MAP_TIMEOUT"));
    QTest::qWait(30);
    QCOMPARE(failed.size(), 0);

    QString exposed;
    for (const QNetworkRequest &request : std::as_const(manager.requests)) {
        exposed += request.url().adjusted(QUrl::RemoveQuery).toString();
    }
    exposed += tencentError.code + tencentError.message
        + networkError.code + networkError.message
        + malformedError.code + malformedError.message
        + timeoutError.code + timeoutError.message;
    QVERIFY(!exposed.contains(key));
}

void TencentMapClientTest::destructionCancelsExternalManagerReplyWithoutLateSignals()
{
    FakeNetworkAccessManager manager;
    manager.plans = {{{}, QNetworkReply::NoError, true}};
    auto *client = new TencentMapClient(
        QStringLiteral("test-only-key"), &manager,
        QUrl(QStringLiteral("https://offline.invalid/geocode")), 20);
    QSignalSpy succeeded(client, &TencentMapClient::geocodeSucceeded);
    QSignalSpy failed(client, &TencentMapClient::geocodeFailed);

    const QString requestId = client->geocode(QStringLiteral("北京理工大学中关村校区"));
    QVERIFY(!requestId.isEmpty());
    QVERIFY(manager.lastReply != nullptr);
    QNetworkReply *reply = manager.lastReply.data();
    QSignalSpy finished(reply, &QNetworkReply::finished);
    QSignalSpy destroyed(reply, &QObject::destroyed);
    bool aborted = false;
    connect(reply, &QNetworkReply::finished, &manager, [reply, &aborted] {
        aborted = reply->error() == QNetworkReply::OperationCanceledError;
    });

    delete client;

    QCOMPARE(finished.count(), 1);
    QVERIFY(aborted);
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(destroyed.count(), 1, 500);
    QVERIFY(manager.lastReply.isNull());
    QTest::qWait(50);
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void TencentMapClientTest::navigationScriptsAreJsonEscapedValidatedAndKeyFreeInCompletionState()
{
    const QString key = QStringLiteral("runtime value '\"\n+&=");
    const QString configure = NavigationPage::buildConfigureMapScript(key, QStringLiteral("configure-7"));
    QVERIFY(configure.contains(QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("operationId"), QStringLiteral("configure-7")},
        {QStringLiteral("config"), QJsonObject{{QStringLiteral("key"), key}}},
    }).toJson(QJsonDocument::Compact))));
    QVERIFY(configure.contains(QStringLiteral("window.configureMap")));
    QVERIFY(configure.contains(QStringLiteral("state:'success'")));
    QVERIFY(configure.contains(QStringLiteral("state:'error'")));
    QVERIFY(!configure.contains(QStringLiteral("message:")));

    QString error;
    const ev::user::GeoPoint from{39.958, 116.317};
    const ev::user::GeoPoint to{39.968, 116.327};
    const QString stationName = QStringLiteral("站点'\"\n</script>");
    const QString route = NavigationPage::buildRenderRouteScript(
        from, to, QStringLiteral("walking"), stationName, QStringLiteral("route-8"), &error);
    QVERIFY(error.isEmpty());
    QVERIFY(route.contains(QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("operationId"), QStringLiteral("route-8")},
        {QStringLiteral("route"), QJsonObject{
             {QStringLiteral("from"), QJsonObject{{QStringLiteral("lat"), from.latitude}, {QStringLiteral("lng"), from.longitude}}},
             {QStringLiteral("to"), QJsonObject{{QStringLiteral("lat"), to.latitude}, {QStringLiteral("lng"), to.longitude}}},
             {QStringLiteral("mode"), QStringLiteral("walking")},
             {QStringLiteral("stationName"), stationName},
         }},
    }).toJson(QJsonDocument::Compact))));
    QVERIFY(route.contains(
        QStringLiteral("window.renderRoute(operation.route,operation.operationId)")));
    QVERIFY(route.contains(
        QStringLiteral("Promise.resolve(window.renderRoute(operation.route,operation.operationId))")));
    QVERIFY(!route.contains(
        QStringLiteral("then(()=>window.renderRoute(operation.route,operation.operationId))")));

    QVERIFY(NavigationPage::buildRenderRouteScript(
        {91.0, 116.0}, to, QStringLiteral("driving"), stationName, QStringLiteral("bad-1"), &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("坐标")));
    QVERIFY(NavigationPage::buildRenderRouteScript(
        from, to, QStringLiteral("cycling"), stationName, QStringLiteral("bad-2"), &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("驾车或步行")));

    const QString completion = NavigationPage::buildOperationStatusScript(QStringLiteral("route-8"));
    QVERIFY(completion.contains(QStringLiteral("route-8")));
    QVERIFY(!completion.contains(key));
    const QString invalidate = NavigationPage::buildInvalidateRouteScript(
        QStringLiteral("route-8"));
    QVERIFY(invalidate.contains(QStringLiteral("window.invalidateRouteAttempt")));
    QVERIFY(invalidate.contains(QStringLiteral("route-8")));
    const QString reset = NavigationPage::buildResetRouteSessionScript();
    QVERIFY(reset.contains(QStringLiteral("window.resetRouteSession")));
    QVERIFY(!reset.contains(key));
}

void TencentMapClientTest::routeOperationCorrelationCachesOnlyMatchingSuccessAndRetainsLastSuccess()
{
    RouteOperationTracker tracker;
    LastRoute first{{39.958, 116.317}, {39.968, 116.327}, QStringLiteral("第一站"), QStringLiteral("driving"), {}};
    tracker.begin(QStringLiteral("route-1"), first);
    QVERIFY(!tracker.complete(QStringLiteral("stale-route"), QStringLiteral("success"),
                              QDateTime::fromString(QStringLiteral("2026-09-02T08:00:00+08:00"), Qt::ISODate)));
    QVERIFY(!tracker.lastSuccessfulRoute().has_value());
    QVERIFY(tracker.complete(QStringLiteral("route-1"), QStringLiteral("success"),
                             QDateTime::fromString(QStringLiteral("2026-09-02T08:01:00+08:00"), Qt::ISODate)));
    QVERIFY(tracker.lastSuccessfulRoute().has_value());
    QCOMPARE(tracker.lastSuccessfulRoute()->stationName, QStringLiteral("第一站"));
    QVERIFY(tracker.lastSuccessfulRoute()->generatedAt.isValid());

    LastRoute second{{39.958, 116.317}, {39.978, 116.337}, QStringLiteral("第二站"), QStringLiteral("walking"), {}};
    tracker.begin(QStringLiteral("route-2"), second);
    QVERIFY(tracker.complete(QStringLiteral("route-2"), QStringLiteral("error"), {}));
    QCOMPARE(tracker.lastSuccessfulRoute()->stationName, QStringLiteral("第一站"));
    QVERIFY(tracker.retryRoute().has_value());
    QCOMPARE(tracker.retryRoute()->stationName, QStringLiteral("第二站"));

    tracker.begin(QStringLiteral("route-timeout"), second);
    tracker.invalidatePending();
    QVERIFY(!tracker.complete(QStringLiteral("route-timeout"), QStringLiteral("success"),
                              QDateTime::currentDateTime()));
    QCOMPARE(tracker.lastSuccessfulRoute()->stationName, QStringLiteral("第一站"));

    NavigationPage page(QStringLiteral("invalid-replacement-key"));
    page.routeOperationId_ = QStringLiteral("route-old-pending");
    page.routeTracker_.begin(QStringLiteral("route-old-pending"), second);
    page.showRoute({91.0, 116.3}, station(9, false, 1.0));
    QVERIFY(page.routeOperationId_.isEmpty());
    QVERIFY(!page.routeTracker_.complete(
        QStringLiteral("route-old-pending"), QStringLiteral("success"),
        QDateTime::currentDateTime()));

    tracker.resetForSession();
    QVERIFY(!tracker.lastSuccessfulRoute().has_value());
    QVERIFY(!tracker.retryRoute().has_value());
    QVERIFY(!tracker.complete(QStringLiteral("route-2"), QStringLiteral("success"),
                              QDateTime::currentDateTime()));
}

void TencentMapClientTest::realNavigationPageRunsQrcPromisePollingAndRetryOffline()
{
    const QString bootstrap = QStringLiteral(R"JS(
        window.__offlineNavigation = {configureCalls: 0, routes: [], operationIds: [], failNext: false};
        window.configureMap = function(config) {
            window.__offlineNavigation.configureCalls += 1;
            return Promise.resolve();
        };
        window.renderRoute = function(route, operationId) {
            window.__offlineNavigation.routes.push(route);
            window.__offlineNavigation.operationIds.push(operationId);
            if (window.__offlineNavigation.failNext) {
                window.__offlineNavigation.failNext = false;
                return Promise.reject(new Error('offline planned failure'));
            }
            return Promise.resolve();
        };
    )JS");
    NavigationPage page(QStringLiteral("offline map test value +&="), bootstrap, nullptr);
    page.resize(640, 480);
    page.show();

    auto *view = page.findChild<QWebEngineView *>(QStringLiteral("navigationWebView"));
    auto *status = page.findChild<QLabel *>(QStringLiteral("navigationStatus"));
    auto *cache = page.findChild<QLabel *>(QStringLiteral("lastRouteLabel"));
    auto *retry = page.findChild<QPushButton *>(QStringLiteral("navigationRetryButton"));
    QVERIFY(view != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(cache != nullptr);
    QVERIFY(retry != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(view->url(), NavigationPage::pageUrl(), 5'000);
    QVERIFY(view->settings()->testAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls));
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("地图已加载"), 5'000);

    bool completed = false;
    QCOMPARE(runJavaScriptAndWait(
                 view->page(), QStringLiteral("window.__offlineNavigation.configureCalls"), &completed)
                 .toInt(),
             1);
    QVERIFY(completed);

    const ev::user::GeoPoint origin{39.958, 116.317};
    ev::user::Station destination = station(9, false, 1.0);
    destination.name = QStringLiteral("离线测试站");
    destination.latitude = 39.968;
    destination.longitude = 116.327;
    page.showRoute(origin, destination, QStringLiteral("driving"));
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("路线规划成功"), 5'000);
    QVERIFY(page.lastSuccessfulRoute().has_value());
    QCOMPARE(page.lastSuccessfulRoute()->mode, QStringLiteral("driving"));
    QVERIFY(cache->text().contains(QStringLiteral("上次成功路线")));

    runJavaScriptAndWait(
        view->page(), QStringLiteral("window.__offlineNavigation.failNext = true"), &completed);
    QVERIFY(completed);
    page.showRoute(origin, destination, QStringLiteral("walking"));
    QTRY_COMPARE_WITH_TIMEOUT(status->text(),
                              QStringLiteral("路线规划失败，请检查网络后重试"), 5'000);
    QVERIFY(retry->isVisible());
    QVERIFY(page.lastSuccessfulRoute().has_value());
    QCOMPARE(page.lastSuccessfulRoute()->mode, QStringLiteral("driving"));
    QVERIFY(cache->text().contains(QStringLiteral("上次成功路线")));

    retry->click();
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("路线规划成功"), 5'000);
    QVERIFY(page.lastSuccessfulRoute().has_value());
    QCOMPARE(page.lastSuccessfulRoute()->mode, QStringLiteral("walking"));
    QCOMPARE(runJavaScriptAndWait(
                 view->page(), QStringLiteral("window.__offlineNavigation.configureCalls"), &completed)
                 .toInt(),
             1);
    QVERIFY(completed);
    QCOMPARE(runJavaScriptAndWait(
                 view->page(), QStringLiteral("window.__offlineNavigation.routes.length"), &completed)
                 .toInt(),
             3);
    QVERIFY(completed);
    const QVariantList operationIds = runJavaScriptAndWait(
        view->page(), QStringLiteral("window.__offlineNavigation.operationIds"), &completed)
        .toList();
    QVERIFY(completed);
    QCOMPARE(operationIds.size(), 3);
    QSet<QString> uniqueOperationIds;
    for (const QVariant &operationId : operationIds) {
        QVERIFY(!operationId.toString().isEmpty());
        uniqueOperationIds.insert(operationId.toString());
    }
    QCOMPARE(uniqueOperationIds.size(), 3);
    const QString completionState = runJavaScriptAndWait(
        view->page(), QStringLiteral("JSON.stringify(window.__qtOperations)"), &completed).toString();
    QVERIFY(completed);
    QVERIFY(!completionState.contains(QStringLiteral("offline map test value")));
}

void TencentMapClientTest::navigationPageDestructionIgnoresPendingWebCallbacks()
{
    {
        const QString slowBootstrap = QStringLiteral(R"JS(
            (() => {
                const deadline = Date.now() + 250;
                while (Date.now() < deadline) {}
                window.configureMap = () => Promise.resolve();
                window.renderRoute = () => Promise.resolve();
                return true;
            })()
        )JS");
        auto *page = new NavigationPage(QStringLiteral("destruction-bootstrap-key"),
                                        slowBootstrap, nullptr);
        page->show();
        auto *view = page->findChild<QWebEngineView *>(QStringLiteral("navigationWebView"));
        QVERIFY(view != nullptr);
        QSignalSpy loaded(view, &QWebEngineView::loadFinished);
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 5'000);
        const QPointer<NavigationPage> guardedPage(page);
        delete page;
        QTest::qWait(350);
        QVERIFY(guardedPage.isNull());
    }

    {
        const QString pendingRouteBootstrap = QStringLiteral(R"JS(
            window.configureMap = () => Promise.resolve();
            window.renderRoute = () => new Promise(() => {});
        )JS");
        auto *page = new NavigationPage(QStringLiteral("destruction-route-key"),
                                        pendingRouteBootstrap, nullptr);
        page->show();
        auto *status = page->findChild<QLabel *>(QStringLiteral("navigationStatus"));
        QVERIFY(status != nullptr);
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("地图已加载"), 5'000);
        page->showRoute({39.958, 116.317}, station(9, false, 1.0));
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("正在规划路线…"), 1'000);
        QTest::qWait(75);
        const QPointer<NavigationPage> guardedPage(page);
        delete page;
        QTest::qWait(150);
        QVERIFY(guardedPage.isNull());
    }
}

void TencentMapClientTest::mainWindowTopNavigationInvalidatesHiddenRouteAndPreservesSuccess()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = server.serverPort();
    config.tencentMapKey.clear(); // Forces local validation failure; no live map request.
    MainWindow window(config);
    window.show();
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer != nullptr);
    completeMainLogin(window, peer.data());

    auto *nearby = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    QVERIFY(nearby != nullptr);
    const ev::user::GeoPoint origin{39.958, 116.317};
    ev::user::Station first = station(31, false, 1.0);
    first.name = QStringLiteral("保留成功路线");
    first.latitude = 39.968;
    first.longitude = 116.327;
    emit nearby->navigationRequested(origin, first);
    auto *navigation = window.findChild<NavigationPage *>(QStringLiteral("navigationPage"));
    QVERIFY(navigation != nullptr);
    auto *view = navigation->findChild<QWebEngineView *>(QStringLiteral("navigationWebView"));
    auto *status = navigation->findChild<QLabel *>(QStringLiteral("navigationStatus"));
    auto *cache = navigation->findChild<QLabel *>(QStringLiteral("lastRouteLabel"));
    QVERIFY(view != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(cache != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(navigation->pageLoaded_, 5'000);

    bool completed = false;
    QCOMPARE(runJavaScriptAndWait(view->page(), installDeferredRouteHarness(), &completed)
                 .toString(),
             QStringLiteral("ready"));
    QVERIFY(completed);
    navigation->invalidateRouteAttempt();
    navigation->routeTracker_.invalidatePending();
    navigation->routeOperationId_.clear();
    navigation->configureOperationId_.clear();
    navigation->configured_ = true;

    navigation->showRoute(origin, first, QStringLiteral("driving"));
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("路线规划成功"), 2'000);
    QVERIFY(navigation->lastSuccessfulRoute().has_value());
    QCOMPARE(navigation->lastSuccessfulRoute()->stationName, first.name);

    runJavaScriptAndWait(
        view->page(), QStringLiteral("window.__hardeningRoute.deferred = true"), &completed);
    QVERIFY(completed);
    ev::user::Station pending = first;
    pending.name = QStringLiteral("隐藏页不得提交路线");
    pending.latitude = 39.978;
    pending.longitude = 116.337;
    navigation->showRoute(origin, pending, QStringLiteral("walking"));
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("正在规划路线…"), 1'000);
    const QString pendingOperationId = navigation->routeOperationId_;
    QVERIFY(!pendingOperationId.isEmpty());

    auto *profileButton = window.findChild<QPushButton *>(
        QStringLiteral("profileNavigationButton"));
    QVERIFY(profileButton != nullptr);
    profileButton->click();
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    QVERIFY(pages != nullptr);
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("profilePage"));
    QCOMPARE(status->text(), QStringLiteral("导航已暂停"));

    const QString resolvePending = QStringLiteral(
        "window.__hardeningRoute.pending[%1]?.(); true")
        .arg(jsonString(pendingOperationId));
    QVERIFY(runJavaScriptAndWait(view->page(), resolvePending, &completed).toBool());
    QVERIFY(completed);
    QTest::qWait(200);
    QCOMPARE(status->text(), QStringLiteral("导航已暂停"));
    QVERIFY(navigation->lastSuccessfulRoute().has_value());
    QCOMPARE(navigation->lastSuccessfulRoute()->stationName, first.name);
    QVERIFY(navigation->routeTracker_.retryRoute().has_value());
    QCOMPARE(navigation->routeTracker_.retryRoute()->stationName, pending.name);
    QVERIFY(cache->text().contains(first.name));
    const QString webStatus = runJavaScriptAndWait(
        view->page(), QStringLiteral("document.getElementById('route-status').textContent"),
        &completed).toString();
    QVERIFY(completed);
    QVERIFY(!webStatus.contains(pending.name));
}

void TencentMapClientTest::sessionExpiryClearsNavigationCacheBeforeRelogin()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = server.serverPort();
    config.tencentMapKey.clear(); // Forces local validation failure; no live map request.
    MainWindow window(config);
    window.show();
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer != nullptr);
    completeMainLogin(window, peer.data());

    auto *nearby = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    auto *api = window.findChild<UserApi *>();
    QVERIFY(nearby != nullptr);
    QVERIFY(api != nullptr);
    const ev::user::GeoPoint origin{39.958, 116.317};
    ev::user::Station destination = station(41, false, 1.0);
    destination.name = QStringLiteral("旧账户私有路线");
    destination.latitude = 39.968;
    destination.longitude = 116.327;
    emit nearby->navigationRequested(origin, destination);
    auto *navigation = window.findChild<NavigationPage *>(QStringLiteral("navigationPage"));
    QVERIFY(navigation != nullptr);
    auto *view = navigation->findChild<QWebEngineView *>(QStringLiteral("navigationWebView"));
    auto *status = navigation->findChild<QLabel *>(QStringLiteral("navigationStatus"));
    auto *cache = navigation->findChild<QLabel *>(QStringLiteral("lastRouteLabel"));
    auto *retry = navigation->findChild<QPushButton *>(QStringLiteral("navigationRetryButton"));
    QVERIFY(view != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(cache != nullptr);
    QVERIFY(retry != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(navigation->pageLoaded_, 5'000);

    bool completed = false;
    QCOMPARE(runJavaScriptAndWait(view->page(), installDeferredRouteHarness(), &completed)
                 .toString(),
             QStringLiteral("ready"));
    QVERIFY(completed);
    navigation->invalidateRouteAttempt();
    navigation->routeTracker_.invalidatePending();
    navigation->routeOperationId_.clear();
    navigation->configureOperationId_.clear();
    navigation->configured_ = true;
    navigation->showRoute(origin, destination, QStringLiteral("driving"));
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("路线规划成功"), 2'000);
    QVERIFY(navigation->lastSuccessfulRoute().has_value());
    QVERIFY(cache->text().contains(destination.name));
    QCOMPARE(runJavaScriptAndWait(
                 view->page(), QStringLiteral("window.__hardeningRoute.overlay"), &completed)
                 .toBool(),
             true);
    QVERIFY(completed);

    auto *nearbyButton = window.findChild<QPushButton *>(
        QStringLiteral("nearbyNavigationButton"));
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    QVERIFY(nearbyButton != nullptr);
    QVERIFY(pages != nullptr);
    nearbyButton->click();
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("nearbyPage"));
    QVERIFY(navigation->lastSuccessfulRoute().has_value());
    QCOMPARE(navigation->lastSuccessfulRoute()->stationName, destination.name);
    QVERIFY(runJavaScriptAndWait(
                view->page(), QStringLiteral("window.__hardeningRoute.overlay"), &completed)
                .toBool());
    QVERIFY(completed);

    const auto expiryContext = api->loadOrderHistory(20, 0, 99, 101);
    QVERIFY(!expiryContext.requestId.isEmpty());
    const auto expiryRequest = takeRequest(peer.data());
    QCOMPARE(expiryRequest.action, QStringLiteral("order.list"));
    reply(peer.data(), expiryRequest.requestId, false, QStringLiteral("AUTH_REQUIRED"),
          QStringLiteral("expired"), QJsonObject{});
    QTRY_COMPARE_WITH_TIMEOUT(pages->currentWidget()->objectName(),
                              QStringLiteral("loginPage"), 1'000);
    QTRY_COMPARE_WITH_TIMEOUT(
        runJavaScriptAndWait(
            view->page(), QStringLiteral("window.__hardeningRoute.resetCount"), &completed)
            .toInt(),
        1, 2'000);
    QVERIFY(completed);
    QVERIFY(!navigation->lastSuccessfulRoute().has_value());
    QVERIFY(!navigation->routeTracker_.retryRoute().has_value());
    QCOMPARE(cache->text(), QStringLiteral("暂无成功路线"));
    QVERIFY(retry->isHidden());
    QVERIFY(!runJavaScriptAndWait(
                 view->page(), QStringLiteral("window.__hardeningRoute.overlay"), &completed)
                 .toBool());
    QVERIFY(completed);

    completeMainLogin(window, peer.data(), 77);
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("nearbyPage"));
    QVERIFY(!navigation->lastSuccessfulRoute().has_value());
    QVERIFY(!navigation->routeTracker_.retryRoute().has_value());
    QCOMPARE(cache->text(), QStringLiteral("暂无成功路线"));
    QVERIFY(retry->isHidden());
    QVERIFY(!runJavaScriptAndWait(
                 view->page(), QStringLiteral("window.__hardeningRoute.overlay"), &completed)
                 .toBool());
    QVERIFY(completed);
}

void TencentMapClientTest::resourceAndPageContractsRemainFixedAndDisplayPredictionStates()
{
    QCOMPARE(NavigationPage::pageUrl(), QUrl(QStringLiteral("qrc:/map/navigation.html")));
    QFile resource(QStringLiteral(":/map/navigation.html"));
    QVERIFY(resource.exists());
    QVERIFY(resource.open(QIODevice::ReadOnly));
    const QByteArray html = resource.readAll();
    QVERIFY(html.contains("window.configureMap"));
    QVERIFY(html.contains("window.renderRoute"));
    QVERIFY(html.contains("window.resetRouteSession"));
    QVERIFY(html.contains("window.TMap.MultiMarker"));
    QVERIFY(!html.contains("id=\"route-retry\""));
    QVERIFY(!html.contains("retryCurrentOperation"));

    QCOMPARE(NearbyPage::presetAddresses(), QStringList({
        QStringLiteral("北京理工大学中关村校区"),
        QStringLiteral("北京航空航天大学学院路校区"),
        QStringLiteral("北京市海淀区西二旗"),
    }));

    const auto disabled = station(8, false, 0.8);
    ev::user::ForecastLatestResult none;
    QCOMPARE(NearbyPage::forecastText(disabled, none), QStringLiteral("暂无预测"));

    const auto enabled = station(1, true, 0.2);
    none.forecastRun = ev::user::ForecastRun{
        QStringLiteral("run"), QStringLiteral("2026-09-01T08:00:00+08:00"),
        QStringLiteral("2026-09-01T07:00:00+08:00"), QStringLiteral("2026-09-01T08:01:00+08:00"),
        QStringLiteral("v1"), QString(64, QLatin1Char('b')), true};
    none.records.append({1, QStringLiteral("2026-09-01T08:00:00+08:00"), 1, 75.5, 2, 2,
                         QStringLiteral("medium"), false});
    const QString stale = NearbyPage::forecastText(enabled, none);
    QVERIFY(stale.contains(QStringLiteral("1小时")));
    QVERIFY(stale.contains(QStringLiteral("2")));
    QVERIFY(stale.contains(QStringLiteral("已过期")));
}

void TencentMapClientTest::nearbyChargerButtonsLocalizeEveryWireStatus()
{
    NearbyPage page(nullptr, nullptr);
    const ev::user::GeoPoint origin{39.958, 116.317};
    ev::user::Station chosenStation = station(2, false, 1.2);
    chosenStation.chargerCount = 6;
    chosenStation.idleCount = 1;
    page.displayStations({origin, {chosenStation}});

    const QList<QPair<QString, QString>> expectedLabels{
        {QStringLiteral("idle"), QStringLiteral("空闲")},
        {QStringLiteral("reserved"), QStringLiteral("已预约")},
        {QStringLiteral("charging"), QStringLiteral("充电中")},
        {QStringLiteral("fault"), QStringLiteral("故障")},
        {QStringLiteral("restarting"), QStringLiteral("重启中")},
        {QStringLiteral("unexpected"), QStringLiteral("状态未知")},
    };
    QVector<ev::user::Charger> chargers;
    for (qsizetype index = 0; index < expectedLabels.size(); ++index) {
        chargers.append(charger(30 + index, chosenStation.stationId,
                                expectedLabels.at(index).first));
    }
    page.displayStationDetail({chosenStation, chargers});

    for (qsizetype index = 0; index < expectedLabels.size(); ++index) {
        const auto *button = page.findChild<QPushButton *>(
            QStringLiteral("chargerButton_%1").arg(30 + index));
        QVERIFY(button != nullptr);
        QVERIFY2(button->text().contains(expectedLabels.at(index).second),
                 qPrintable(button->text()));
        for (const auto &wireAndLabel : expectedLabels) {
            QVERIFY2(!button->text().contains(wireAndLabel.first),
                     qPrintable(button->text()));
        }
    }
}

void TencentMapClientTest::nearbySelectionCarriesOriginStationAndChargerWithoutMutation()
{
    NearbyPage page(nullptr, nullptr);
    QSignalSpy selected(&page, &NearbyPage::chargerSelected);
    QSignalSpy navigation(&page, &NearbyPage::navigationRequested);
    const ev::user::GeoPoint origin{39.958, 116.317};
    const ev::user::Station chosenStation = station(2, false, 1.2);
    page.displayStations({origin, {chosenStation}});
    page.displayStationDetail({chosenStation, {charger(1001, 2)}});

    auto *forecastLabel = page.findChild<QLabel *>(QStringLiteral("forecastLabel_2"));
    QVERIFY(forecastLabel != nullptr);
    QCOMPARE(forecastLabel->text(), QStringLiteral("暂无预测"));
    auto *chargerButton = page.findChild<QPushButton *>(QStringLiteral("chargerButton_1001"));
    auto *navigateButton = page.findChild<QPushButton *>(QStringLiteral("navigateButton"));
    QVERIFY(chargerButton != nullptr);
    QVERIFY(chargerButton->text().contains(QStringLiteral("充电桩 ID：1001")));
    QVERIFY(navigateButton != nullptr);
    chargerButton->click();
    QCOMPARE(selected.size(), 1);
    const auto selection = qvariant_cast<ev::user::StationSelection>(selected.takeFirst().at(0));
    QCOMPARE(selection.origin.latitude, origin.latitude);
    QCOMPARE(selection.station.stationId, qint64{2});
    QCOMPARE(selection.charger.chargerId, qint64{1001});
    QVERIFY(selection.selectionGeneration > 0);
    chargerButton->click();
    QCOMPARE(selected.size(), 1);
    const auto nextSelection =
        qvariant_cast<ev::user::StationSelection>(selected.takeFirst().at(0));
    QVERIFY(nextSelection.selectionGeneration > selection.selectionGeneration);
    navigateButton->click();
    QCOMPARE(navigation.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::GeoPoint>(navigation.first().at(0)).longitude, origin.longitude);
    QCOMPARE(qvariant_cast<ev::user::Station>(navigation.first().at(1)).stationId, qint64{2});
}

int main(int argc, char *argv[])
{
    WebEngineRuntime::applySystemCompatibility();
    QApplication application(argc, argv);
    TencentMapClientTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_tencentmap.moc"
