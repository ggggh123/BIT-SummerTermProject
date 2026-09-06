#pragma once
#include "core/Result.h"
#include "protocol/Envelope.h"
#include <QObject>
#include <QJsonObject>
#include <QSet>
#include <memory>

enum class AdminView { Summary, Stations, Chargers, Users, RequestLog };

class DatabaseWorker : public QObject {
    Q_OBJECT
public:
    DatabaseWorker();
    ~DatabaseWorker() override;
    Result start(const QString &databasePath, const QString &snapshotPath,
                 const QString &goldenPath = {}, const QString &goldenHash = {});
    QString databasePath() const;
    QJsonObject health() const;
    void execute(quint64 sequence, const ev::protocol::RequestEnvelope &request);
    void query(quint64 sequence, AdminView view, const QString &token, const QJsonObject &parameters);
    void stop();
signals:
    void completed(quint64 sequence, QByteArray response);
    void healthChanged(QJsonObject health);
private:
    void refreshHealth();
    void scheduleRestart(int chargerId);
    void finishStop();
    struct State;
    std::unique_ptr<State> m_state;
    QSet<int> m_restarts;
    QJsonObject m_health;
    bool m_stopping = false;
    quint64 m_resetGeneration = 0;
};
