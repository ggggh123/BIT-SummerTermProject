// 真实 MainWindow + 真实腾讯地图；仅项目业务 API 使用本地 TCP 测试响应。
// 显式运行，不注册为默认 CTest。Key 从现有本机配置读取，截图不包含配置值。
#include "app/UserAppConfig.h"
#include "app/WebEngineRuntime.h"
#include "net/TencentMapClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "ui/MainWindow.h"
#include "ui/NavigationPage.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebEnginePage>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QtTest>

#include <atomic>
#include <functional>

namespace {

const QString kTimestamp = QStringLiteral("2026-09-05T10:00:00+08:00");

QJsonObject smokeStation()
{
    return {{"stationId", 1}, {"name", QStringLiteral("地图冒烟测试站（模拟业务数据）")},
            {"address", QStringLiteral("北京海淀测试目的地，非真实运营站点")},
            {"latitude", 39.969}, {"longitude", 116.319}, {"priceFenPerKwh", 135},
            {"forecastEnabled", false}, {"chargerCount", 1}, {"idleCount", 1}};
}

QJsonObject smokeCharger()
{
    return {{"chargerId", 1001}, {"stationId", 1}, {"code", "SMOKE-1001"},
            {"type", "fast"}, {"powerKw", 60.0}, {"status", "idle"},
            {"chargeCount", 0}, {"totalDurationSec", 0}, {"updatedAt", kTimestamp}};
}

class MapFailureInterceptor final : public QWebEngineUrlRequestInterceptor
{
public:
    std::atomic<bool> enabled{false};
    std::atomic<int> blocked{0};

    void interceptRequest(QWebEngineUrlRequestInfo &info) override
    {
        const QString host = info.requestUrl().host();
        if (enabled && (host == QStringLiteral("qq.com")
                        || host.endsWith(QStringLiteral(".qq.com")))) {
            ++blocked;
            info.block(true);
        }
    }
};

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 30'000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeoutMs) {
        QTest::qWait(20);
    }
    return condition();
}

QVariant pageValue(QWebEnginePage *page, const QString &script)
{
    QVariant value;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    // Context guard avoids capturing stack variables after a timed-out callback.
    QPointer<QEventLoop> guard(&loop);
    page->runJavaScript(script, [guard, &value](const QVariant &result) {
        if (guard) {
            value = result;
            guard->quit();
        }
    });
    timeout.start(5'000);
    loop.exec();
    return value;
}

} // namespace

class OnlineMapSmoke final : public QObject
{
    Q_OBJECT
private slots:
    void productionWindowOnlineNavigation();
};

void OnlineMapSmoke::productionWindowOnlineNavigation()
{
    UserAppConfig config = UserAppConfig::load();
    if (config.tencentMapKey.isEmpty()) {
        QFAIL("未配置腾讯 Key；请通过 EV_TENCENT_MAP_KEY 或 config.local.ini 注入后显式运行。");
    }
    const QString output = qEnvironmentVariable("EV_MAP_SMOKE_OUTPUT_DIR");
    QVERIFY2(!output.isEmpty() && QDir::isAbsolutePath(output),
             "EV_MAP_SMOKE_OUTPUT_DIR 必须为绝对路径，保存脱敏截图。");
    QVERIFY(QDir().mkpath(output));
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QStringList actions;
    QStringList unsupported;
    connect(&server, &QTcpServer::newConnection, this, [&] {
        auto *peer = server.nextPendingConnection();
        auto decoder = std::make_shared<ev::protocol::FrameDecoder>();
        connect(peer, &QTcpSocket::readyRead, &server, [&, peer, decoder] {
            for (const QByteArray &frame : decoder->append(peer->readAll())) {
                const auto request = ev::protocol::parseRequest(frame);
                actions.append(request.action);
                QJsonObject data;
                if (request.action == QStringLiteral("auth.user_login")) {
                    data = {{"token", "local-smoke-session"}, {"user", QJsonObject{
                        {"userId", 1}, {"mobile", request.payload.value("mobile")},
                        {"nickname", QStringLiteral("地图冒烟测试用户")}, {"avatarPath", ""},
                        {"balanceFen", 10000}, {"status", "active"}, {"registeredAt", kTimestamp}}}};
                } else if (request.action == QStringLiteral("order.current")) {
                    data = {{"order", QJsonValue(QJsonValue::Null)}};
                } else if (request.action == QStringLiteral("station.list")) {
                    QJsonObject station = smokeStation();
                    station.insert("distanceKm", 1.5);
                    data = {{"stations", QJsonArray{station}}};
                } else if (request.action == QStringLiteral("forecast.latest")) {
                    data = {{"forecastRun", QJsonValue(QJsonValue::Null)}, {"records", QJsonArray{}}};
                } else if (request.action == QStringLiteral("station.detail")) {
                    data = {{"station", smokeStation()}, {"chargers", QJsonArray{smokeCharger()}}};
                } else {
                    unsupported.append(request.action);
                }
                const bool ok = !unsupported.contains(request.action);
                const auto response = ev::protocol::toJson({request.requestId, ok,
                    ok ? QStringLiteral("OK") : QStringLiteral("INVALID_REQUEST"),
                    QStringLiteral("本地冒烟测试响应，非真实项目服务端"), data});
                peer->write(ev::protocol::encodeFrame(response));
            }
        });
    });
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = server.serverPort();
    config.validationErrors.clear();
    // Interceptor must outlive the window/page which refers to it.
    MapFailureInterceptor interceptor;
    MainWindow window(config);
    window.setWindowTitle(QStringLiteral("客户端地图冒烟｜业务 API 为本地测试响应"));
    window.resize(1100, 800);
    window.show();
    const auto save = [&](const QString &name) {
        return window.grab().save(QDir(output).filePath(name + QStringLiteral(".png")));
    };
    auto *pages = window.findChild<QStackedWidget *>("mainPages");
    auto *phone = window.findChild<QLineEdit *>("phoneEdit");
    auto *login = window.findChild<QPushButton *>("loginButton");
    auto *search = window.findChild<QPushButton *>("nearbySearchButton");
    auto *address = window.findChild<QComboBox *>("addressBox");
    auto *nearbyStatus = window.findChild<QLabel *>("nearbyStatus");
    auto *mapClient = window.findChild<TencentMapClient *>();
    QVERIFY(pages && phone && login && search && address && nearbyStatus && mapClient);
    QVERIFY(waitUntil([&] { return search->isEnabled(); }, 5'000));
    phone->setText(QStringLiteral("13800138000"));
    login->click();
    QVERIFY(waitUntil([&] { return pages->currentWidget()->objectName() == "nearbyPage"; }, 5'000));
    QSignalSpy geocoded(mapClient, &TencentMapClient::geocodeSucceeded);
    QSignalSpy geocodeFailed(mapClient, &TencentMapClient::geocodeFailed);
    address->setCurrentText(QStringLiteral("北京理工大学中关村校区"));
    search->click();
    QVERIFY(waitUntil([&] { return !geocoded.isEmpty() || !geocodeFailed.isEmpty(); }, 8'000));
    QVERIFY(save(QStringLiteral("01-geocode")));
    QVERIFY2(!geocoded.isEmpty(), qPrintable(nearbyStatus->text()));
    const auto point = qvariant_cast<ev::user::GeoPoint>(geocoded.first().at(1));
    QVERIFY(point.latitude > 39.8 && point.latitude < 40.1);
    QVERIFY(point.longitude > 116.2 && point.longitude < 116.5);
    qInfo().noquote() << "真实腾讯地址解析坐标：" << point.latitude << point.longitude;
    QVERIFY(waitUntil([&] { return window.findChild<QPushButton *>("stationButton_1") != nullptr; }, 5'000));
    auto *prediction = window.findChild<QLabel *>("forecastLabel_1");
    QVERIFY(prediction && prediction->text() == QStringLiteral("暂无预测"));
    window.findChild<QPushButton *>("stationButton_1")->click();
    QVERIFY(waitUntil([&] { return window.findChild<QPushButton *>("navigateButton") != nullptr; }, 5'000));
    QVERIFY(window.findChild<QPushButton *>("chargerButton_1001"));
    QVERIFY(save(QStringLiteral("02-station-detail")));
    window.findChild<QPushButton *>("navigateButton")->click();
    auto *navigation = window.findChild<NavigationPage *>("navigationPage");
    QVERIFY(navigation);
    auto *view = navigation->findChild<QWebEngineView *>("navigationWebView");
    auto *status = navigation->findChild<QLabel *>("navigationStatus");
    auto *cache = navigation->findChild<QLabel *>("lastRouteLabel");
    auto *mode = navigation->findChild<QComboBox *>("routeModeBox");
    auto *retry = navigation->findChild<QPushButton *>("navigationRetryButton");
    QVERIFY(view && status && cache && mode && retry);
    view->page()->setUrlRequestInterceptor(&interceptor);
    const auto routeReady = [&](const QString &expectedMode) {
        return waitUntil([&] {
            const auto route = navigation->lastSuccessfulRoute();
            return (route && route->mode == expectedMode && status->text() == QStringLiteral("路线规划成功"))
                || !retry->isHidden();
        });
    };
    QVERIFY(routeReady(QStringLiteral("driving")));
    // Route service completion precedes asynchronous basemap tiles/camera paint.
    QTest::qWait(5'000);
    QVERIFY(save(QStringLiteral("03-driving")));
    QVERIFY2(navigation->lastSuccessfulRoute().has_value(), qPrintable(status->text()));
    QCOMPARE(view->url(), QUrl(QStringLiteral("qrc:/map/navigation.html")));
    QCOMPARE(navigation->lastSuccessfulRoute()->mode, QStringLiteral("driving"));
    QCOMPARE(pageValue(view->page(), "window.lastRouteStatus.state").toString(), QStringLiteral("success"));
    qInfo() << "真实腾讯驾车路线成功";

    mode->setCurrentIndex(mode->findData(QStringLiteral("walking")));
    QVERIFY(routeReady(QStringLiteral("walking")));
    QTest::qWait(1'000);
    QVERIFY(save(QStringLiteral("04-walking")));
    QCOMPARE(navigation->lastSuccessfulRoute()->mode, QStringLiteral("walking"));
    QCOMPARE(pageValue(view->page(), "window.lastRouteStatus.lastSuccessfulRequest.mode").toString(),
             QStringLiteral("walking"));
    qInfo() << "真实腾讯步行路线成功";

    interceptor.enabled = true;
    mode->setCurrentIndex(mode->findData(QStringLiteral("driving")));
    QVERIFY(waitUntil([&] { return !retry->isHidden(); }));
    QVERIFY(interceptor.blocked > 0);
    QVERIFY(save(QStringLiteral("05-network-failure-cache")));
    QCOMPARE(navigation->lastSuccessfulRoute()->mode, QStringLiteral("walking"));
    QVERIFY(cache->text().contains(QStringLiteral("步行")));
    QCOMPARE(pageValue(view->page(), "window.lastRouteStatus.state").toString(), QStringLiteral("error"));
    QVERIFY(pageValue(view->page(), "document.getElementById('route-empty').textContent")
                .toString().contains(QStringLiteral("上次成功路线")));
    qInfo() << "网络层阻断验证：失败提示、重试按钮和上次成功步行路线均保留";

    interceptor.enabled = false;
    retry->click();
    QVERIFY(routeReady(QStringLiteral("driving")));
    QCOMPARE(navigation->lastSuccessfulRoute()->mode, QStringLiteral("driving"));
    QVERIFY(retry->isHidden());
    QVERIFY(save(QStringLiteral("06-retry-success")));
    QVERIFY(unsupported.isEmpty());
    QVERIFY(!actions.contains(QStringLiteral("forecast.latest"))); // Disabled forecast is a valid core path.
    qInfo() << "解除阻断后真实驾车重试成功；业务链路仅为测试响应，不代表真实后端联调";
}

int main(int argc, char **argv)
{
    WebEngineRuntime::applySystemCompatibility();
    QApplication app(argc, argv);
    OnlineMapSmoke test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_online_map_smoke.moc"
