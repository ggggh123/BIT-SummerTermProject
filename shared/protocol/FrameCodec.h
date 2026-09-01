#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QtGlobal>

#include <stdexcept>

namespace ev::protocol {

inline constexpr quint32 MaxPayloadBytes = 1024U * 1024U;

class FrameError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

QByteArray encodeFrame(QByteArrayView payload);

class FrameDecoder
{
public:
    QList<QByteArray> append(QByteArrayView bytes);
    void reset();

private:
    QByteArray buffer_;
};

} // namespace ev::protocol
