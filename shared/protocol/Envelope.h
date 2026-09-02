#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace ev::protocol {

struct RequestEnvelope
{
    int version;
    QString requestId;
    QString action;
    QString token;
    QJsonObject payload;
};

struct ResponseEnvelope
{
    QString requestId;
    bool ok;
    QString code;
    QString message;
    QJsonValue data;
};

} // namespace ev::protocol
