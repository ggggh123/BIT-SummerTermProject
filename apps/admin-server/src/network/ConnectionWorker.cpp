#include "network/ConnectionWorker.h"
#include "protocol/JsonEnvelope.h"
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
ConnectionWorker::ConnectionWorker(qintptr descriptor, bool reject)
    : m_descriptor(descriptor), m_reject(reject) {}
void ConnectionWorker::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] { stop(); });
    if (!m_socket->setSocketDescriptor(m_descriptor)) { stop(); return; }
    if (m_reject) {
        reply(ev::protocol::toJson({{},false,"SERVER_BUSY","连接容量已满，请稍后重试",QJsonObject{}}));
        m_socket->disconnectFromHost();
        QTimer::singleShot(1000,this,&ConnectionWorker::stop);
        return;
    }
    connect(m_socket, &QTcpSocket::readyRead, this, &ConnectionWorker::read);
    if (m_socket->bytesAvailable()) read();
}
void ConnectionWorker::stop()
{
    if (m_closed) return;
    m_closed = true;
    if (m_socket) m_socket->abort();
    emit closed();
}
void ConnectionWorker::reply(const QByteArray &bytes)
{
    if (!m_closed && m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(ev::protocol::encodeFrame(bytes));
}
void ConnectionWorker::read()
{
    QList<QByteArray> frames;
    try { frames = m_decoder.append(m_socket->readAll()); }
    catch (const ev::protocol::FrameError &) {
        reply(ev::protocol::toJson({{},false,"INVALID_REQUEST","无效 TCP 帧",QJsonObject{}}));
        m_socket->disconnectFromHost();
        return;
    }
    for (const auto &frame : frames) {
        try { emit requestReceived(ev::protocol::parseRequest(frame)); }
        catch (const ev::protocol::EnvelopeError &error) {
            reply(ev::protocol::toJson({{},false,error.code(),error.message(),QJsonObject{}}));
        }
    }
}
