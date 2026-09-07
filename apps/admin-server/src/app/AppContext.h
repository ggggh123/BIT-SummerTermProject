#pragma once
#include "core/Result.h"
#include "db/DatabaseWorker.h"
#include "network/ApiServer.h"
#include <QObject>
#include <QThread>
#include <QPointer>
#include <QHash>
#include <functional>
#include <memory>
#include <atomic>

class AppContext : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString databasePath;
        QString host = QStringLiteral("127.0.0.1");
        quint16 port = 9100;
        QString snapshotPath;
        QString goldenPath;
        QString goldenHash;
    };
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;
    Result initialize();
    Result initialize(const Options &options);
    void shutdown();
    void executeLocal(ev::protocol::RequestEnvelope request, QObject *receiver, std::function<void(QByteArray)> callback);
    void queryAdmin(AdminView view, const QString &token, const QJsonObject &parameters,
                    QObject *receiver, std::function<void(QByteArray)> callback);
    QJsonObject healthSnapshot() const;
    ApiServer *apiServer() const;
    QString databasePath() const;
    QString host() const;
    quint16 port() const;
private:
    struct Pending { QPointer<QObject> receiver; std::function<void(QByteArray)> callback; };
    quint64 enqueue(QObject *receiver, std::function<void(QByteArray)> callback);
    void deliver(quint64 sequence, const QByteArray &bytes);
    QThread m_databaseThread;
    DatabaseWorker *m_worker = nullptr;
    std::unique_ptr<ApiServer> m_apiServer;
    QHash<quint64, Pending> m_pending;
    quint64 m_sequence = 0;
    QJsonObject m_health;
    TokenRoles m_tokenRoles;
    QString m_databasePath;
    QString m_host;
    quint16 m_port = 0;
    std::shared_ptr<std::atomic_bool> m_accepting = std::make_shared<std::atomic_bool>(false);
};
