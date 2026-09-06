#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "app/RuntimeStatusWriter.h"
#include "app/SimulatorConfig.h"
#include "core/TelemetryEngine.h"
#include "net/SimulatorClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"

using namespace ev::protocol;
using namespace ev::simulator;

namespace {

QJsonObject readStatus(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

class LoopbackStatusServer : public QObject
{
    Q_OBJECT
public:
    explicit LoopbackStatusServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&server_, &QTcpServer::newConnection,
                this, &LoopbackStatusServer::acceptConnection);
    }

    bool listen()
    {
        return server_.listen(QHostAddress::LocalHost);
    }

    quint16 port() const
    {
        return server_.serverPort();
    }

    void setAutoReply(bool enabled)
    {
        autoReply_ = enabled;
    }

    void setStatusCode(const QString &code)
    {
        statusCode_ = code;
    }

    void disconnectClient()
    {
        if (connection_)
            connection_->abort();
    }

private slots:
    void acceptConnection()
    {
        connection_ = server_.nextPendingConnection();
        connect(connection_, &QTcpSocket::readyRead,
                this, &LoopbackStatusServer::readRequests);
    }

    void readRequests()
    {
        const QList<QByteArray> frames = decoder_.append(connection_->readAll());
        for (const QByteArray &frame : frames) {
            const RequestEnvelope request = parseRequest(frame);
            if (request.action == QStringLiteral("simulator.status") && autoReply_)
                replyToStatus(request.requestId);
        }
    }

private:
    void replyToStatus(const QString &requestId)
    {
        ResponseEnvelope response;
        response.requestId = requestId;
        response.ok = statusCode_ == QLatin1String("OK");
        response.code = statusCode_;
        response.message = response.ok
            ? QStringLiteral("ok") : QStringLiteral("rejected");

        QJsonObject data;
        if (response.ok) {
            QJsonObject charger;
            charger[QStringLiteral("chargerId")] = 1001;
            charger[QStringLiteral("status")] = QStringLiteral("idle");
            charger[QStringLiteral("powerKw")] = 60.0;
            QJsonArray chargers;
            chargers.append(charger);
            data[QStringLiteral("chargers")] = chargers;
        }
        response.data = data;
        connection_->write(encodeFrame(toJson(response)));
    }

    QTcpServer server_;
    QTcpSocket *connection_ = nullptr;
    FrameDecoder decoder_;
    bool autoReply_ = true;
    QString statusCode_ = QStringLiteral("OK");
};

TelemetryEngine makeEngine()
{
    return TelemetryEngine(
        20260901,
        QDateTime::fromString(
            QStringLiteral("2026-09-06T10:00:00+08:00"), Qt::ISODate),
        10000);
}

SimulatorConfig makeConfig(quint16 port)
{
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port;
    config.intervalMs = 10000;
    config.token = QStringLiteral("test-token");
    return config;
}

} // namespace

class RuntimeStatusTest : public QObject
{
    Q_OBJECT
private slots:
    void writesStartingWithSchemaPidAndUtcTimestamp();
    void connectedWaitsForAuthenticatedSnapshot();
    void successfulSnapshotsRefreshReadyTimestampThenDisconnect();
    void authenticationFailureIsObservable();
    void stopIsObservable();
    void reportsWriteFailure();
    void emptyStatusPathDoesNotWrite();
    void commandLineTokenOverridesEnvironment();
    void environmentTokenIsFallback();
    void emptyTokenRemainsCompatible();
};

void RuntimeStatusTest::writesStartingWithSchemaPidAndUtcTimestamp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("runtime.json"));

    TelemetryEngine engine = makeEngine();
    SimulatorConfig config;
    SimulatorClient client(config, &engine);
    RuntimeStatusWriter writer(path, &client);

    QString error;
    QVERIFY2(writer.start(&error), qPrintable(error));
    const QJsonObject state = readStatus(path);
    QCOMPARE(state.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(state.value(QStringLiteral("pid")).toInteger(),
             QCoreApplication::applicationPid());
    QCOMPARE(state.value(QStringLiteral("sessionState")).toString(),
             QStringLiteral("starting"));

    const QString updatedAt = state.value(QStringLiteral("updatedAt")).toString();
    QVERIFY(updatedAt.endsWith(QLatin1Char('Z')));
    QVERIFY(updatedAt.contains(QLatin1Char('.')));
    const QDateTime parsed = QDateTime::fromString(updatedAt, Qt::ISODateWithMs);
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.offsetFromUtc(), 0);
}

void RuntimeStatusTest::connectedWaitsForAuthenticatedSnapshot()
{
    QTemporaryDir dir;
    LoopbackStatusServer server;
    server.setAutoReply(false);
    QVERIFY(dir.isValid());
    QVERIFY(server.listen());

    TelemetryEngine engine = makeEngine();
    SimulatorClient client(makeConfig(server.port()), &engine);
    RuntimeStatusWriter writer(dir.filePath(QStringLiteral("runtime.json")), &client);
    QVERIFY(writer.start());

    client.start();
    QTRY_COMPARE(readStatus(writer.filePath())
                     .value(QStringLiteral("sessionState")).toString(),
                 QStringLiteral("waiting_auth"));
    QTest::qWait(100);
    QCOMPARE(readStatus(writer.filePath())
                 .value(QStringLiteral("sessionState")).toString(),
             QStringLiteral("waiting_auth"));
    client.stop();
}

void RuntimeStatusTest::successfulSnapshotsRefreshReadyTimestampThenDisconnect()
{
    QTemporaryDir dir;
    LoopbackStatusServer server;
    QVERIFY(dir.isValid());
    QVERIFY(server.listen());

    TelemetryEngine engine = makeEngine();
    SimulatorClient client(makeConfig(server.port()), &engine);
    RuntimeStatusWriter writer(dir.filePath(QStringLiteral("runtime.json")), &client);
    QVERIFY(writer.start());

    client.start();
    QTRY_COMPARE(readStatus(writer.filePath())
                     .value(QStringLiteral("sessionState")).toString(),
                 QStringLiteral("ready"));
    const QString firstUpdate = readStatus(writer.filePath())
                                    .value(QStringLiteral("updatedAt")).toString();

    QTest::qWait(5);
    client.refresh();
    QTRY_VERIFY(readStatus(writer.filePath())
                    .value(QStringLiteral("updatedAt")).toString() != firstUpdate);

    server.disconnectClient();
    QTRY_COMPARE(readStatus(writer.filePath())
                     .value(QStringLiteral("sessionState")).toString(),
                 QStringLiteral("disconnected"));
    client.stop();
}

void RuntimeStatusTest::authenticationFailureIsObservable()
{
    QTemporaryDir dir;
    LoopbackStatusServer server;
    server.setStatusCode(QStringLiteral("AUTH_REQUIRED"));
    QVERIFY(dir.isValid());
    QVERIFY(server.listen());

    TelemetryEngine engine = makeEngine();
    SimulatorClient client(makeConfig(server.port()), &engine);
    RuntimeStatusWriter writer(dir.filePath(QStringLiteral("runtime.json")), &client);
    QVERIFY(writer.start());

    client.start();
    QTRY_COMPARE(readStatus(writer.filePath())
                     .value(QStringLiteral("sessionState")).toString(),
                 QStringLiteral("auth_failed"));
    client.stop();
}

void RuntimeStatusTest::stopIsObservable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TelemetryEngine engine = makeEngine();
    SimulatorConfig config;
    SimulatorClient client(config, &engine);
    RuntimeStatusWriter writer(dir.filePath(QStringLiteral("runtime.json")), &client);
    QVERIFY(writer.start());

    QVERIFY(writer.stop());
    QCOMPARE(readStatus(writer.filePath())
                 .value(QStringLiteral("sessionState")).toString(),
             QStringLiteral("stopped"));
}

void RuntimeStatusTest::reportsWriteFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("missing/runtime.json"));
    TelemetryEngine engine = makeEngine();
    SimulatorConfig config;
    SimulatorClient client(config, &engine);
    RuntimeStatusWriter writer(path, &client);

    QString error;
    QVERIFY(!writer.start(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(path));
}

void RuntimeStatusTest::emptyStatusPathDoesNotWrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString oldCurrent = QDir::currentPath();
    QVERIFY(QDir::setCurrent(dir.path()));

    TelemetryEngine engine = makeEngine();
    SimulatorConfig config;
    SimulatorClient client(config, &engine);
    RuntimeStatusWriter writer(QString(), &client);
    QString error;
    QVERIFY2(writer.start(&error), qPrintable(error));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 0);

    QVERIFY(QDir::setCurrent(oldCurrent));
}

static void verifyConfigProbe(const QStringList &arguments,
                              const QString &environmentToken,
                              const QString &expectedToken)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (environmentToken.isNull())
        environment.remove(QStringLiteral("EV_SIMULATOR_TOKEN"));
    else
        environment.insert(QStringLiteral("EV_SIMULATOR_TOKEN"), environmentToken);
    process.setProcessEnvironment(environment);

    QStringList probeArguments{
        QStringLiteral("--config-probe"),
        expectedToken.isEmpty() ? QStringLiteral("__EMPTY__") : expectedToken
    };
    probeArguments.append(arguments);
    process.start(QCoreApplication::applicationFilePath(), probeArguments);
    QVERIFY2(process.waitForFinished(3000), qPrintable(process.errorString()));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
}

void RuntimeStatusTest::commandLineTokenOverridesEnvironment()
{
    verifyConfigProbe({QStringLiteral("--token"), QStringLiteral("cli-token")},
                      QStringLiteral("env-token"), QStringLiteral("cli-token"));
}

void RuntimeStatusTest::environmentTokenIsFallback()
{
    verifyConfigProbe({}, QStringLiteral("env-token"), QStringLiteral("env-token"));
}

void RuntimeStatusTest::emptyTokenRemainsCompatible()
{
    verifyConfigProbe({}, QString(), QString());
}

int main(int argc, char **argv)
{
    if (argc >= 3 && QByteArray(argv[1]) == QByteArrayLiteral("--config-probe")) {
        const QString expected = QByteArray(argv[2]) == QByteArrayLiteral("__EMPTY__")
            ? QString() : QString::fromLocal8Bit(argv[2]);
        QList<QByteArray> argumentStorage;
        argumentStorage.append(QByteArray(argv[0]));
        for (int i = 3; i < argc; ++i)
            argumentStorage.append(QByteArray(argv[i]));
        QVector<char *> childArguments;
        childArguments.reserve(argumentStorage.size());
        for (QByteArray &argument : argumentStorage)
            childArguments.append(argument.data());
        int childArgc = childArguments.size();
        QCoreApplication app(childArgc, childArguments.data());
        return configFromCommandLine(app).token == expected ? 0 : 1;
    }

    QCoreApplication app(argc, argv);
    RuntimeStatusTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_runtimestatus.moc"
