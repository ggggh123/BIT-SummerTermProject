#include "protocol/Envelope.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace {

quint16 availablePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return probe.serverPort();
}

QByteArray waitForOutputContaining(QProcess *process, const QByteArray &needle)
{
    QByteArray output;
    for (int attempt = 0; attempt < 50 && !output.contains(needle); ++attempt) {
        process->waitForReadyRead(100);
        output += process->readAllStandardOutput();
        output += process->readAllStandardError();
    }
    return output;
}

void stopProcess(QProcess *process)
{
    process->terminate();
    if (!process->waitForFinished(3000)) {
        process->kill();
        process->waitForFinished(3000);
    }
}

class ProcessGuard
{
public:
    explicit ProcessGuard(QProcess *process)
        : m_process(process)
    {
    }

    ~ProcessGuard()
    {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            stopProcess(m_process);
        }
    }

private:
    QProcess *m_process;
};

} // namespace

class AdminRuntimeTest : public QObject
{
    Q_OBJECT

private slots:
    void configurationOptionsKeepGuiAndUseOneRuntimeDatabase()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString databasePath = tempDir.filePath(QStringLiteral("runtime.db"));
        const QString snapshotPath = tempDir.filePath(QStringLiteral("snapshot.json"));
        QVERIFY(QFile::copy(QStringLiteral(EV_TEST_GOLDEN_DB), databasePath));
        const quint16 port = availablePort();
        QVERIFY(port > 0);

        QProcess guiProcess;
        ProcessGuard guiGuard(&guiProcess);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        guiProcess.setProcessEnvironment(environment);
        guiProcess.start(QStringLiteral(EV_ADMIN_SERVER_PATH),
                         {QStringLiteral("--db"), databasePath,
                          QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                          QStringLiteral("--port"), QString::number(port),
                          QStringLiteral("--snapshot"), snapshotPath});
        QVERIFY2(guiProcess.waitForStarted(3000), qPrintable(guiProcess.errorString()));

        const QByteArray startup = waitForOutputContaining(&guiProcess, QByteArrayLiteral("mode=gui"));
        QVERIFY2(startup.contains(QByteArrayLiteral("mode=gui")), startup.constData());
        QVERIFY2(startup.contains(databasePath.toUtf8()), startup.constData());
        QVERIFY2(startup.contains(snapshotPath.toUtf8()), startup.constData());

        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY2(socket.waitForConnected(3000), qPrintable(socket.errorString()));
        const QString requestId = QStringLiteral("admin-runtime-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        const ev::protocol::RequestEnvelope request{
            1,
            requestId,
            QStringLiteral("system.health"),
            QString(),
            QJsonObject{}};
        const QByteArray payload = ev::protocol::toJson(request);
        socket.write(ev::protocol::encodeFrame(QByteArrayView(payload.constData(), payload.size())));
        QVERIFY(socket.waitForBytesWritten(3000));
        QVERIFY(socket.waitForReadyRead(3000));

        ev::protocol::FrameDecoder decoder;
        const QByteArray replyBytes = socket.readAll();
        const QList<QByteArray> frames = decoder.append(QByteArrayView(replyBytes.constData(), replyBytes.size()));
        QCOMPARE(frames.size(), 1);
        const ev::protocol::ResponseEnvelope response = ev::protocol::parseResponse(frames.first());
        QVERIFY(response.ok);
        QCOMPARE(response.requestId, requestId);

        QProcess duplicate;
        ProcessGuard duplicateGuard(&duplicate);
        duplicate.setProcessEnvironment(environment);
        duplicate.start(QStringLiteral(EV_ADMIN_SERVER_PATH),
                        {QStringLiteral("--server"),
                         QStringLiteral("--db"), databasePath,
                         QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                         QStringLiteral("--port"), QString::number(port),
                         QStringLiteral("--snapshot"), snapshotPath});
        QVERIFY(duplicate.waitForStarted(3000));
        QVERIFY(duplicate.waitForFinished(3000));
        QCOMPARE(duplicate.exitCode(), 1);

        socket.disconnectFromHost();
        stopProcess(&guiProcess);

        const QString connectionName = QStringLiteral("admin-runtime-db-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(databasePath);
            QVERIFY(database.open());
            QSqlQuery query(database);
            query.prepare(QStringLiteral("SELECT COUNT(*) FROM request_log WHERE request_id=?"));
            query.addBindValue(requestId);
            QVERIFY(query.exec());
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 1);
            database.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    void noGuiFlagRemainsExplicitlyHeadless()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString databasePath = tempDir.filePath(QStringLiteral("runtime.db"));
        QVERIFY(QFile::copy(QStringLiteral(EV_TEST_GOLDEN_DB), databasePath));
        const quint16 port = availablePort();
        QVERIFY(port > 0);

        QProcess process;
        ProcessGuard guard(&process);
        process.start(QStringLiteral(EV_ADMIN_SERVER_PATH),
                      {QStringLiteral("--no-gui"),
                       QStringLiteral("--db"), databasePath,
                       QStringLiteral("--port"), QString::number(port)});
        QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
        const QByteArray startup = waitForOutputContaining(&process, QByteArrayLiteral("mode=headless"));
        QVERIFY2(startup.contains(QByteArrayLiteral("mode=headless")), startup.constData());
        QVERIFY2(startup.contains(databasePath.toUtf8()), startup.constData());
    }
};

QTEST_MAIN(AdminRuntimeTest)
#include "tst_admin_runtime.moc"
