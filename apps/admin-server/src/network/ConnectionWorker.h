#pragma once
#include "protocol/Envelope.h"
#include "protocol/FrameCodec.h"
#include <QObject>
class QTcpSocket;
Q_DECLARE_METATYPE(ev::protocol::RequestEnvelope)
class ConnectionWorker : public QObject {
    Q_OBJECT
public:
    explicit ConnectionWorker(qintptr descriptor, bool reject = false);
    void start();
    void stop();
    void reply(const QByteArray &bytes);
signals:
    void requestReceived(ev::protocol::RequestEnvelope request);
    void closed();
private:
    void read();
    qintptr m_descriptor;
    bool m_reject;
    bool m_closed = false;
    QTcpSocket *m_socket = nullptr;
    ev::protocol::FrameDecoder m_decoder;
};
