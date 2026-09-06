#pragma once
#include "network/ConnectionWorker.h"
#include <QTcpServer>
#include <QHash>
class QThread;
class ApiServer : public QTcpServer {
    Q_OBJECT
public:
    explicit ApiServer(QObject *parent = nullptr);
    ~ApiServer() override;
    void stop();
signals:
    void requestReceived(ConnectionWorker *connection, ev::protocol::RequestEnvelope request);
protected:
    void incomingConnection(qintptr descriptor) override;
private:
    void retire(QThread *thread);
    QHash<QThread *, ConnectionWorker *> m_connections;
    bool m_stopping = false;
};
