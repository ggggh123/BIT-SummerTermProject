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

QByteArray toJson(const RequestEnvelope &request);
QByteArray toJson(const ResponseEnvelope &response);
RequestEnvelope parseRequest(QByteArrayView json);
ResponseEnvelope parseResponse(QByteArrayView json);

} // namespace ev::protocol
