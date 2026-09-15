#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace ev::protocol {

// 请求信封：帧 body 的 JSON 结构（requestId 配对响应，token 鉴权，action 路由）。
struct RequestEnvelope
{
    int version;
    QString requestId;
    QString action;
    QString token;
    QJsonObject payload;
};

// 响应信封：ok+code 表示成功/失败码，data 携带业务结果。
struct ResponseEnvelope
{
    QString requestId;
    bool ok;
    QString code;
    QString message;
    QJsonValue data;
};

} // namespace ev::protocol
