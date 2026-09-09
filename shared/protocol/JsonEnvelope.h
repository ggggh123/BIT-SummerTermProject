#pragma once

#include "protocol/Envelope.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <stdexcept>

namespace ev::protocol {

class EnvelopeError final : public std::runtime_error
{
public:
    EnvelopeError(QString code, QString message);

    QString code() const;
    QString message() const;

private:
    QString code_;
    QString message_;
};

// JSON 信封层：帧 body 的统一格式 {requestId, action, token, payload}，
// requestId 用于请求-响应配对，token 用于鉴权。
QByteArray toJson(const RequestEnvelope &request);
QByteArray toJson(const ResponseEnvelope &response);
RequestEnvelope parseRequest(QByteArrayView json);
ResponseEnvelope parseResponse(QByteArrayView json);

} // namespace ev::protocol
