#include "protocol/FrameCodec.h"

#include <QtEndian>

#include <algorithm>

namespace ev::protocol {

QByteArray encodeFrame(QByteArrayView payload)
{
    if (payload.isEmpty() || payload.size() > MaxPayloadBytes) {
        throw FrameError("invalid payload length");
    }

    QByteArray frame;
    frame.resize(4 + payload.size());
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), frame.data());
    std::copy(payload.begin(), payload.end(), frame.begin() + 4);
    return frame;
}

QList<QByteArray> FrameDecoder::append(QByteArrayView bytes)
{
    buffer_.append(bytes.data(), bytes.size());

    QList<QByteArray> frames;
    while (buffer_.size() >= 4) {
        const auto length = qFromBigEndian<quint32>(buffer_.constData());
        if (length == 0 || length > MaxPayloadBytes) {
            throw FrameError("invalid payload length");
        }

        if (buffer_.size() < 4 + static_cast<int>(length)) {
            break;
        }

        frames.append(buffer_.mid(4, static_cast<int>(length)));
        buffer_.remove(0, 4 + static_cast<int>(length));
    }

    return frames;
}

void FrameDecoder::reset()
{
    buffer_.clear();
}

} // namespace ev::protocol
