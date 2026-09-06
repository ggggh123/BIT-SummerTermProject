#include "network/ApiServer.h"
#include <QThread>
ApiServer::ApiServer(QObject *parent) : QTcpServer(parent)
{
    qRegisterMetaType<ev::protocol::RequestEnvelope>();
}
ApiServer::~ApiServer() { stop(); }
void ApiServer::incomingConnection(qintptr descriptor)
{
    auto *thread = new QThread(this);
    auto *connection = new ConnectionWorker(descriptor, m_connections.size() >= 16);
    connection->moveToThread(thread);
    m_connections.insert(thread, connection);
    connect(thread,&QThread::started,connection,&ConnectionWorker::start);
    connect(thread,&QThread::finished,connection,&QObject::deleteLater);
    connect(connection,&ConnectionWorker::requestReceived,this,[this,connection](const auto &request) {
        if (!m_stopping) emit requestReceived(connection,request);
    });
    connect(connection,&ConnectionWorker::closed,this,[this,thread] { retire(thread); });
    thread->start();
}
void ApiServer::retire(QThread *thread)
{
    if (!m_connections.contains(thread)) return;
    m_connections.remove(thread);
    thread->quit();
    thread->wait();
    thread->deleteLater();
}
void ApiServer::stop()
{
    if (m_stopping) return;
    m_stopping = true;
    close();
    const auto threads = m_connections.keys();
    for (auto *thread : threads) {
        auto *connection = m_connections.value(thread);
        QMetaObject::invokeMethod(connection, &ConnectionWorker::stop, Qt::BlockingQueuedConnection);
        retire(thread);
    }
}
