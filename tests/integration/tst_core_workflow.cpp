#include "app/SimulatorConfig.h"
#include "core/TelemetryEngine.h"
#include "domain/ContractTimestamp.h"
#include "net/SimulatorClient.h"
#include "net/TcpJsonClient.h"
#include "protocol/Envelope.h"
#include "services/UserApi.h"
#include "ui/SimulatorWindow.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>

namespace {

using ev::protocol::ResponseEnvelope;
using ev::simulator::ChargerSnapshot;
using ev::simulator::SimulatorClient;
using ev::simulator::SimulatorConfig;
using ev::simulator::SimulatorWindow;
using ev::simulator::TelemetryEngine;

constexpr int kWaitMs = 12'000;
const QString kSimulatorToken = QStringLiteral("sim-token");

bool waitUntil(const std::function<bool()> &condition, int timeoutMs = kWaitMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
    return condition();
}

quint16 availablePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return probe.serverPort();
}

QString statusFor(const TelemetryEngine &engine, qint64 chargerId)
{
    for (const ChargerSnapshot &charger : engine.chargers()) {
        if (charger.chargerId == chargerId) {
            return charger.status;
        }
    }
    return {};
}

int rowForCharger(QTableWidget *table, qint64 chargerId)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)
            && table->item(row, 0)->text().toLongLong() == chargerId) {
            return row;
        }
    }
    return -1;
}

class RawClient final : public QObject
{
public:
    explicit RawClient(quint16 port)
    {
        client_.configure(QStringLiteral("127.0.0.1"), port);
        connect(&client_, &TcpJsonClient::connectionChanged, this,
                [this](bool connected) { connected_ = connected; });
        connect(&client_, &TcpJsonClient::responseReceived, this,
                [this](const ResponseEnvelope &response) {
                    responses_.insert(response.requestId, response);
                });
        connect(&client_, &TcpJsonClient::transportFailed, this,
                [this](const QString &requestId, const QString &code,
                       const QString &message) {
                    responses_.insert(requestId,
                                      {requestId, false, code, message, QJsonObject{}});
                });
    }

    ~RawClient() override { client_.disconnectFromServer(); }

    bool open()
    {
        client_.connectToServer();
        if (!waitUntil([this] { return connected_; })) {
            error_ = QStringLiteral("TCP connection timeout");
            return false;
        }
        return true;
    }

    std::optional<ResponseEnvelope> request(const QString &action,
                                            const QJsonObject &payload = {},
                                            const QString &token = {})
    {
        const QString requestId = client_.send(action, payload, token);
        if (!waitUntil([this, &requestId] { return responses_.contains(requestId); })) {
            error_ = QStringLiteral("response timeout for %1").arg(action);
            return std::nullopt;
        }
        const ResponseEnvelope response = responses_.take(requestId);
        if (!response.ok) {
            error_ = QStringLiteral("%1 returned %2: %3")
                         .arg(action, response.code, response.message);
        }
        return response;
    }

    QString error() const { return error_; }

private:
    TcpJsonClient client_;
    QHash<QString, ResponseEnvelope> responses_;
    QString error_;
    bool connected_ = false;
};

class SimulatorClientGuard final
{
public:
    explicit SimulatorClientGuard(SimulatorClient *client)
        : client_(client)
    {
    }

    ~SimulatorClientGuard()
    {
        if (client_) {
            client_->stop();
        }
    }

private:
    SimulatorClient *client_;
};

class UserSession final
{
public:
    explicit UserSession(quint16 port)
        : api_(&client_)
    {
        client_.configure(QStringLiteral("127.0.0.1"), port);
    }

    ~UserSession() { client_.disconnectFromServer(); }

    bool open()
    {
        bool connected = false;
        const QMetaObject::Connection connection = QObject::connect(
            &api_, &UserApi::connectionChanged,
            [&connected](bool current) { connected = current; });
        client_.connectToServer();
        const bool ok = waitUntil([&connected] { return connected; });
        QObject::disconnect(connection);
        if (!ok) {
            error_ = QStringLiteral("UserApi connection timeout");
        }
        return ok;
    }

    bool login(const QString &mobile, ev::user::User *user)
    {
        QSignalSpy success(&api_, &UserApi::loginSucceeded);
        QSignalSpy failure(&api_, &UserApi::requestFailed);
        api_.loginByPhone(mobile);
        if (!waitForEither(success, failure, QStringLiteral("login"))) {
            return false;
        }
        if (!failure.isEmpty()) {
            return rememberApiError(failure.takeFirst().at(0).value<ev::user::ApiError>());
        }
        *user = success.takeFirst().at(0).value<ev::user::User>();
        return true;
    }

    bool recharge(const QString &amount, qint64 *balanceFen)
    {
        QSignalSpy success(&api_, &UserApi::sessionUserApplied);
        QSignalSpy failure(&api_, &UserApi::profileRequestFailed);
        (void)api_.rechargeWallet(amount);
        if (!waitForEither(success, failure, QStringLiteral("wallet.recharge"))) {
            return false;
        }
        if (!failure.isEmpty()) {
            return rememberApiError(failure.takeFirst().at(0).value<ev::user::ApiError>());
        }
        *balanceFen = success.takeLast().at(0).value<ev::user::User>().balanceFen;
        return true;
    }

    bool stationDetail(qint64 stationId, ev::user::StationDetailResult *detail)
    {
        QSignalSpy success(&api_, &UserApi::stationDetailLoaded);
        QSignalSpy failure(&api_, &UserApi::requestFailed);
        const QString requestId = api_.loadStationDetail(stationId);
        if (requestId.isEmpty()
            || !waitForEither(success, failure, QStringLiteral("station.detail"))) {
            return false;
        }
        if (!failure.isEmpty()) {
            return rememberApiError(failure.takeFirst().at(0).value<ev::user::ApiError>());
        }
        *detail = success.takeFirst().at(1).value<ev::user::StationDetailResult>();
        return true;
    }

    bool reserve(qint64 chargerId, ev::user::Order *order)
    {
        return mutate(QStringLiteral("charge.reserve"),
                      [this, chargerId] { (void)api_.reserveCharger(chargerId, 1, 1); }, order);
    }

    bool start(qint64 orderId, ev::user::Order *order)
    {
        return mutate(QStringLiteral("charge.start"),
                      [this, orderId] { (void)api_.startCharging(orderId, 1, 1); }, order);
    }

    bool stop(qint64 orderId, ev::user::Order *order)
    {
        return mutate(QStringLiteral("charge.stop"),
                      [this, orderId] { (void)api_.stopCharging(orderId, 1, 1); }, order);
    }

    bool settle(qint64 orderId, ev::user::Order *order, qint64 *balanceFen)
    {
        QSignalSpy success(&api_, &UserApi::chargeSettled);
        QSignalSpy failure(&api_, &UserApi::chargeRequestFailed);
        (void)api_.settleCharging(orderId, 1, 1);
        if (!waitForEither(success, failure, QStringLiteral("charge.settle"))) {
            return false;
        }
        if (!failure.isEmpty()) {
            return rememberApiError(failure.takeFirst().at(1).value<ev::user::ApiError>());
        }
        const QList<QVariant> values = success.takeFirst();
        *order = values.at(1).value<ev::user::Order>();
        *balanceFen = values.at(2).toLongLong();
        return true;
    }

    bool current(std::optional<ev::user::Order> *order)
    {
        QSignalSpy success(&api_, &UserApi::currentOrderLoaded);
        QSignalSpy failure(&api_, &UserApi::chargeRequestFailed);
        (void)api_.loadCurrentOrder(1, 1, ev::user::ChargeOperation::Poll);
        if (!waitForEither(success, failure, QStringLiteral("order.current"))) {
            return false;
        }
        if (!failure.isEmpty()) {
            return rememberApiError(failure.takeFirst().at(1).value<ev::user::ApiError>());
        }
        *order = success.takeFirst().at(1).value<ev::user::CurrentOrderResult>().order;
        return true;
    }

    bool history(ev::user::OrderListResult *history)
    {
        QSignalSpy success(&api_, &UserApi::orderHistoryLoaded);
        QSignalSpy failure(&api_, &UserApi::orderHistoryRequestFailed);
        (void)api_.loadOrderHistory(100, 0, 1, 1);
        if (!waitForEither(success, failure, QStringLiteral("order.list"))) {
            return false;
        }
        if (!failure.isEmpty()) {
            return rememberApiError(failure.takeFirst().at(1).value<ev::user::ApiError>());
        }
        *history = success.takeFirst().at(1).value<ev::user::OrderListResult>();
        return true;
    }

    QString error() const { return error_; }

private:
    bool mutate(const QString &name, const std::function<void()> &operation,
                ev::user::Order *order)
    {
        QSignalSpy success(&api_, &UserApi::chargeOrderChanged);
        QSignalSpy failure(&api_, &UserApi::chargeRequestFailed);
        operation();
        if (!waitForEither(success, failure, name)) {
            return false;
        }
        if (!failure.isEmpty()) {
            return rememberApiError(failure.takeFirst().at(1).value<ev::user::ApiError>());
        }
        *order = success.takeFirst().at(1).value<ev::user::Order>();
        return true;
    }

    bool waitForEither(const QSignalSpy &success, const QSignalSpy &failure,
                       const QString &operation)
    {
        if (waitUntil([&success, &failure] {
                return !success.isEmpty() || !failure.isEmpty();
            })) {
            return true;
        }
        error_ = QStringLiteral("timeout waiting for %1").arg(operation);
        return false;
    }

    bool rememberApiError(const ev::user::ApiError &error)
    {
        error_ = QStringLiteral("%1: %2").arg(error.code, error.message);
        return false;
    }

    TcpJsonClient client_;
    UserApi api_;
    QString error_;
};

qint64 firstIdleCharger(const ev::user::StationDetailResult &detail,
                        qint64 excluded = 0)
{
    for (const ev::user::Charger &charger : detail.chargers) {
        if (charger.status == QStringLiteral("idle") && charger.chargerId != excluded) {
            return charger.chargerId;
        }
    }
    return 0;
}

bool containsCompleted(const ev::user::OrderListResult &history, qint64 orderId)
{
    for (const ev::user::Order &order : history.items) {
        if (order.orderId == orderId && order.status == QStringLiteral("completed")) {
            return true;
        }
    }
    return false;
}

} // namespace

class CoreWorkflowTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void newUserCompletesRealQtWorkflow();
    void faultRecoveryRequiresAdminRestartAndSettlement();
    void cleanupTestCase();

private:
    bool adminRequest(const QString &action, const QJsonObject &payload,
                      ResponseEnvelope *response);
    qint64 todayRevenue();

    std::unique_ptr<QTemporaryDir> runtimeDir_;
    std::unique_ptr<QProcess> server_;
    std::unique_ptr<RawClient> admin_;
    QString adminToken_;
    QString serverOutput_;
    quint16 port_ = 0;
    qint64 positiveChargerId_ = 0;
    QDateTime lastSimulatorTime_;
};

void CoreWorkflowTest::initTestCase()
{
    runtimeDir_ = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/ev-core-workflow-XXXXXX"));
    QVERIFY(runtimeDir_->isValid());
    const QString runtimeDatabase = runtimeDir_->filePath(QStringLiteral("core-runtime.db"));
    QVERIFY2(QFile::copy(QStringLiteral(EV_TEST_GOLDEN_DB), runtimeDatabase),
             qPrintable(QStringLiteral("cannot copy golden database to %1").arg(runtimeDatabase)));

    port_ = availablePort();
    QVERIFY(port_ > 0);
    const QString configuredServer = qEnvironmentVariable(
        "EV_CORE_SERVER_UNDER_TEST", QStringLiteral(EV_ADMIN_SERVER_PATH));
    server_ = std::make_unique<QProcess>();
    server_->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("TZ"), QStringLiteral("Asia/Shanghai"));
    server_->setProcessEnvironment(environment);
    server_->start(configuredServer,
                   {QStringLiteral("--server"),
                    QStringLiteral("--db"), runtimeDatabase,
                    QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                    QStringLiteral("--port"), QString::number(port_),
                    QStringLiteral("--snapshot"),
                    runtimeDir_->filePath(QStringLiteral("dashboard.json"))});
    QVERIFY2(server_->waitForStarted(3000), qPrintable(server_->errorString()));

    QVERIFY2(waitUntil([this] {
        if (server_->state() == QProcess::NotRunning) {
            serverOutput_ += QString::fromUtf8(server_->readAll());
            return true;
        }
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, port_);
        return probe.waitForConnected(100);
    }, 8000), "server did not become observable");
    QVERIFY2(server_->state() != QProcess::NotRunning, qPrintable(serverOutput_));

    admin_ = std::make_unique<RawClient>(port_);
    QVERIFY2(admin_->open(), qPrintable(admin_->error()));
    const auto login = admin_->request(
        QStringLiteral("admin.login"),
        {{QStringLiteral("username"), QStringLiteral("admin")},
         {QStringLiteral("password"), QStringLiteral("123456")}});
    QVERIFY2(login.has_value(), qPrintable(admin_->error()));
    QVERIFY2(login->ok, qPrintable(admin_->error()));
    adminToken_ = login->data.toObject().value(QStringLiteral("token")).toString();
    QVERIFY(!adminToken_.isEmpty());
}

bool CoreWorkflowTest::adminRequest(const QString &action,
                                    const QJsonObject &payload,
                                    ResponseEnvelope *response)
{
    const auto result = admin_->request(action, payload, adminToken_);
    if (!result.has_value()) {
        return false;
    }
    *response = *result;
    return result->ok;
}

qint64 CoreWorkflowTest::todayRevenue()
{
    ResponseEnvelope response;
    if (!adminRequest(QStringLiteral("admin.dashboard"),
                      {{QStringLiteral("rangeDays"), 7}}, &response)) {
        return -1;
    }
    return response.data.toObject()
        .value(QStringLiteral("revenue")).toObject()
        .value(QStringLiteral("todayRevenueFen")).toInteger(-1);
}

void CoreWorkflowTest::newUserCompletesRealQtWorkflow()
{
    UserSession user(port_);
    QVERIFY2(user.open(), qPrintable(user.error()));
    ev::user::User account;
    QVERIFY2(user.login(QStringLiteral("13900009091"), &account), qPrintable(user.error()));
    QCOMPARE(account.balanceFen, 0);

    ev::user::OrderListResult historyBefore;
    QVERIFY2(user.history(&historyBefore), qPrintable(user.error()));
    QCOMPARE(historyBefore.total, 0);

    qint64 chargedBalance = 0;
    QVERIFY2(user.recharge(QStringLiteral("500.00"), &chargedBalance), qPrintable(user.error()));
    QCOMPARE(chargedBalance, 50'000);

    ev::user::StationDetailResult detail;
    QVERIFY2(user.stationDetail(1, &detail), qPrintable(user.error()));
    positiveChargerId_ = firstIdleCharger(detail);
    QVERIFY(positiveChargerId_ > 0);
    const qint64 priceFenPerKwh = detail.station.priceFenPerKwh;
    QVERIFY(priceFenPerKwh > 0);

    const QDateTime sharedStart = QDateTime::currentDateTimeUtc()
                                      .toOffsetFromUtc(8 * 3600).addSecs(1);
    TelemetryEngine firstEngine(20260901, sharedStart, 60'000);
    SimulatorConfig simulatorConfig;
    QCOMPARE(simulatorConfig.intervalMs, 3000);
    simulatorConfig.host = QStringLiteral("127.0.0.1");
    simulatorConfig.port = port_;
    simulatorConfig.intervalMs = 1000;
    simulatorConfig.token = kSimulatorToken;
    SimulatorClient firstClient(simulatorConfig, &firstEngine);
    SimulatorClientGuard firstClientGuard(&firstClient);
    firstClient.start();
    QVERIFY(waitUntil([&firstEngine, this] {
        return statusFor(firstEngine, positiveChargerId_) == QStringLiteral("idle");
    }));

    ev::user::Order order;
    QVERIFY2(user.reserve(positiveChargerId_, &order), qPrintable(user.error()));
    const qint64 orderId = order.orderId;
    QCOMPARE(order.status, QStringLiteral("reserved"));
    QVERIFY2(user.start(orderId, &order), qPrintable(user.error()));
    QCOMPARE(order.status, QStringLiteral("charging"));
    QVERIFY2(waitUntil([&firstEngine, this] {
        return statusFor(firstEngine, positiveChargerId_) == QStringLiteral("charging");
    }, 6000), "first connected SimulatorClient did not periodically sync charging");

    TelemetryEngine engine(20260901, sharedStart, 60'000);
    SimulatorClient client(simulatorConfig, &engine);
    SimulatorClientGuard clientGuard(&client);
    SimulatorWindow window(&client, &engine);
    client.start();
    QVERIFY2(waitUntil([&engine, this] {
        return statusFor(engine, positiveChargerId_) == QStringLiteral("charging");
    }), "second real SimulatorClient received a stale status snapshot");
    firstClient.stop();

    window.toggleRun();
    QCOMPARE(window.runButtonText(), QStringLiteral("Pause"));
    window.doTick();
    window.doTick();
    window.toggleRun();
    QCOMPARE(window.runButtonText(), QStringLiteral("Run"));
    QVERIFY(window.tickCount() >= 2);
    QVERIFY2(waitUntil([&client] { return client.queuedSamples() == 0; }, 20'000),
             "simulator telemetry acknowledgements did not drain");

    int acceptedTelemetry = 0;
    const QString telemetryPrefix = QStringLiteral("response telemetry-%1-")
                                        .arg(positiveChargerId_);
    for (const QString &line : window.logLines()) {
        if (line.startsWith(telemetryPrefix) && line.endsWith(QStringLiteral(" OK"))) {
            ++acceptedTelemetry;
        }
    }
    QVERIFY2(acceptedTelemetry >= 2, "fewer than two charging telemetry samples were accepted");

    std::optional<ev::user::Order> beforeStop;
    QVERIFY2(user.current(&beforeStop), qPrintable(user.error()));
    QVERIFY(beforeStop.has_value());
    QVERIFY(beforeStop->energyKwh > 0.0);
    const qint64 expectedAmount = qRound64(beforeStop->energyKwh * priceFenPerKwh);
    QCOMPARE(beforeStop->amountFen, expectedAmount);
    const qint64 revenueBefore = todayRevenue();
    QVERIFY2(revenueBefore >= 0, qPrintable(admin_->error()));

    ev::user::Order stopped;
    QVERIFY2(user.stop(orderId, &stopped), qPrintable(user.error()));
    QCOMPARE(stopped.energyKwh, beforeStop->energyKwh);
    QCOMPARE(stopped.amountFen, beforeStop->amountFen);
    QVERIFY(!stopped.endedAt.isEmpty());

    ev::user::Order settled;
    qint64 settledBalance = 0;
    QVERIFY2(user.settle(orderId, &settled, &settledBalance), qPrintable(user.error()));
    QCOMPARE(settled.status, QStringLiteral("completed"));
    QCOMPARE(settled.energyKwh, stopped.energyKwh);
    QCOMPARE(settled.amountFen, stopped.amountFen);
    QCOMPARE(chargedBalance - settledBalance, settled.amountFen);

    std::optional<ev::user::Order> current;
    QVERIFY2(user.current(&current), qPrintable(user.error()));
    QVERIFY(!current.has_value());
    ev::user::OrderListResult historyAfter;
    QVERIFY2(user.history(&historyAfter), qPrintable(user.error()));
    QCOMPARE(historyAfter.total, historyBefore.total + 1);
    QVERIFY(containsCompleted(historyAfter, orderId));
    QVERIFY2(user.stationDetail(1, &detail), qPrintable(user.error()));
    const auto releasedCharger = std::find_if(
        detail.chargers.cbegin(), detail.chargers.cend(),
        [this](const ev::user::Charger &charger) {
            return charger.chargerId == positiveChargerId_;
        });
    QVERIFY(releasedCharger != detail.chargers.cend());
    QCOMPARE(releasedCharger->status, QStringLiteral("idle"));
    QCOMPARE(todayRevenue() - revenueBefore, settled.amountFen);
    lastSimulatorTime_ = engine.currentTime();

    qInfo().noquote()
        << QStringLiteral("positive order=%1 charger=%2 station=%3 price=%4 energy=%5 amount=%6 balance=%7->%8 revenueDelta=%9 telemetryAccepted=%10")
               .arg(orderId).arg(positiveChargerId_).arg(detail.station.name)
               .arg(priceFenPerKwh).arg(settled.energyKwh, 0, 'f', 6)
               .arg(settled.amountFen).arg(chargedBalance).arg(settledBalance)
               .arg(settled.amountFen).arg(acceptedTelemetry);
    client.stop();
}

void CoreWorkflowTest::faultRecoveryRequiresAdminRestartAndSettlement()
{
    UserSession owner(port_);
    QVERIFY2(owner.open(), qPrintable(owner.error()));
    ev::user::User account;
    QVERIFY2(owner.login(QStringLiteral("13800138000"), &account), qPrintable(owner.error()));
    const qint64 balanceBefore = account.balanceFen;

    ev::user::StationDetailResult detail;
    QVERIFY2(owner.stationDetail(1, &detail), qPrintable(owner.error()));
    const qint64 chargerId = firstIdleCharger(detail, positiveChargerId_);
    QVERIFY(chargerId > 0);
    const qint64 priceFenPerKwh = detail.station.priceFenPerKwh;

    ev::user::Order order;
    QVERIFY2(owner.reserve(chargerId, &order), qPrintable(owner.error()));
    const qint64 orderId = order.orderId;
    QVERIFY2(owner.start(orderId, &order), qPrintable(owner.error()));

    QDateTime start = QDateTime::currentDateTimeUtc()
                          .toOffsetFromUtc(8 * 3600).addSecs(1);
    if (lastSimulatorTime_.isValid() && start <= lastSimulatorTime_) {
        start = lastSimulatorTime_.addMSecs(1);
    }
    TelemetryEngine engine(20260902, start, 60'000, [] {
        return QDateTime::currentDateTimeUtc().toOffsetFromUtc(8 * 3600);
    });
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port_;
    config.intervalMs = 1000;
    config.token = kSimulatorToken;
    SimulatorClient client(config, &engine);
    SimulatorClientGuard clientGuard(&client);
    SimulatorWindow window(&client, &engine);
    client.start();
    QVERIFY(waitUntil([&engine, chargerId] {
        return statusFor(engine, chargerId) == QStringLiteral("charging");
    }));

    window.toggleRun();
    window.doTick();
    window.toggleRun();
    QVERIFY(waitUntil([&client] { return client.queuedSamples() == 0; }, 20'000));
    const QString selectedTelemetryPrefix = QStringLiteral("response telemetry-%1-")
                                                .arg(chargerId);
    const QStringList meteringLog = window.logLines();
    QVERIFY2(std::any_of(meteringLog.cbegin(), meteringLog.cend(),
                         [&selectedTelemetryPrefix](const QString &line) {
                             return line.startsWith(selectedTelemetryPrefix)
                                 && line.endsWith(QStringLiteral(" OK"));
                         }),
             qPrintable(meteringLog.join(QLatin1Char('\n'))));
    std::optional<ev::user::Order> metered;
    QVERIFY2(owner.current(&metered), qPrintable(owner.error()));
    QVERIFY(metered.has_value());
    QVERIFY(metered->energyKwh > 0.0);
    QCOMPARE(metered->amountFen, qRound64(metered->energyKwh * priceFenPerKwh));

    auto *table = window.findChild<QTableWidget *>();
    QVERIFY(table);
    const int chargerRow = rowForCharger(table, chargerId);
    QVERIFY(chargerRow >= 0);
    table->selectRow(chargerRow);
    const QDateTime beforeFault = engine.currentTime();
    window.injectFault();
    const QDateTime faultAt = engine.currentTime();
    window.injectRecovery();
    const QDateTime recoveryAt = engine.currentTime();
    QVERIFY(faultAt > beforeFault);
    QVERIFY(recoveryAt > faultAt);

    const QString faultAck = QStringLiteral("response fault-%1-%2 OK")
                                 .arg(chargerId).arg(faultAt.toMSecsSinceEpoch());
    const QString recoveryAck = QStringLiteral("response fault-%1-%2 OK")
                                    .arg(chargerId).arg(recoveryAt.toMSecsSinceEpoch());
    QVERIFY2(waitUntil([&window, &faultAck, &recoveryAck] {
        const QStringList log = window.logLines();
        return log.contains(faultAck) && log.contains(recoveryAck);
    }), "fault and recovery did not both receive OK");

    std::optional<ev::user::Order> faultedOrder;
    QVERIFY2(owner.current(&faultedOrder), qPrintable(owner.error()));
    QVERIFY(faultedOrder.has_value());
    QVERIFY(!faultedOrder->endedAt.isEmpty());
    const auto timeOrder = ev::user::compareContractTimestamps(
        faultedOrder->startedAt, faultedOrder->endedAt);
    QVERIFY(timeOrder.has_value());
    QVERIFY(*timeOrder != ev::user::TimestampComparison::Later);
    QCOMPARE(faultedOrder->energyKwh, metered->energyKwh);
    QCOMPARE(faultedOrder->amountFen, metered->amountFen);
    QVERIFY2(owner.stationDetail(1, &detail), qPrintable(owner.error()));
    const auto faultedCharger = std::find_if(
        detail.chargers.cbegin(), detail.chargers.cend(),
        [chargerId](const ev::user::Charger &charger) {
            return charger.chargerId == chargerId;
        });
    QVERIFY(faultedCharger != detail.chargers.cend());
    QCOMPARE(faultedCharger->status, QStringLiteral("fault"));

    ResponseEnvelope restarted;
    QVERIFY2(adminRequest(QStringLiteral("admin.charger_restart"),
                          {{QStringLiteral("chargerId"), chargerId}}, &restarted),
             qPrintable(admin_->error()));
    QCOMPARE(restarted.data.toObject().value(QStringLiteral("charger"))
                 .toObject().value(QStringLiteral("status")).toString(),
             QStringLiteral("restarting"));

    QVERIFY2(waitUntil([&engine, chargerId] {
        return statusFor(engine, chargerId) == QStringLiteral("idle");
    }, 6000), "simulator did not synchronize idle after admin restart");

    UserSession contender(port_);
    QVERIFY2(contender.open(), qPrintable(contender.error()));
    ev::user::User contenderAccount;
    QVERIFY2(contender.login(QStringLiteral("13900009092"), &contenderAccount),
             qPrintable(contender.error()));
    ev::user::Order forbiddenOrder;
    QVERIFY(!contender.reserve(chargerId, &forbiddenOrder));
    QVERIFY2(contender.error().startsWith(QStringLiteral("CHARGER_NOT_AVAILABLE")),
             qPrintable(contender.error()));

    ev::user::Order settled;
    qint64 balanceAfter = 0;
    QVERIFY2(owner.settle(orderId, &settled, &balanceAfter), qPrintable(owner.error()));
    QCOMPARE(settled.status, QStringLiteral("completed"));
    QCOMPARE(settled.energyKwh, metered->energyKwh);
    QCOMPARE(settled.amountFen, metered->amountFen);
    QCOMPARE(balanceBefore - balanceAfter, settled.amountFen);
    QVERIFY2(owner.stationDetail(1, &detail), qPrintable(owner.error()));
    const auto idleCharger = std::find_if(
        detail.chargers.cbegin(), detail.chargers.cend(),
        [chargerId](const ev::user::Charger &charger) {
            return charger.chargerId == chargerId;
        });
    QVERIFY(idleCharger != detail.chargers.cend());
    QCOMPARE(idleCharger->status, QStringLiteral("idle"));

    qInfo().noquote()
        << QStringLiteral("fault order=%1 charger=%2 faultAt=%3 recoveryAt=%4 energy=%5 amount=%6 balance=%7->%8 restart=idle contender=%9")
               .arg(orderId).arg(chargerId)
               .arg(faultAt.toString(Qt::ISODateWithMs), recoveryAt.toString(Qt::ISODateWithMs))
               .arg(settled.energyKwh, 0, 'f', 6).arg(settled.amountFen)
               .arg(balanceBefore).arg(balanceAfter).arg(contender.error());
    client.stop();
}

void CoreWorkflowTest::cleanupTestCase()
{
    const QString runtimePath = runtimeDir_ ? runtimeDir_->path() : QString();
    admin_.reset();
    if (server_ && server_->state() != QProcess::NotRunning) {
        server_->terminate();
        if (!server_->waitForFinished(3000)) {
            server_->kill();
            QVERIFY(server_->waitForFinished(3000));
        }
    }
    QVERIFY(!server_ || server_->state() == QProcess::NotRunning);
    if (server_) {
        serverOutput_ += QString::fromUtf8(server_->readAll());
    }
    server_.reset();
    runtimeDir_.reset();
    QVERIFY(!runtimePath.isEmpty());
    QVERIFY2(!QFileInfo::exists(runtimePath), qPrintable(runtimePath));
    qInfo().noquote()
        << QStringLiteral("cleanup serverState=NotRunning runtimeRemoved=true path=%1")
               .arg(runtimePath);
}

QTEST_MAIN(CoreWorkflowTest)
#include "tst_core_workflow.moc"
