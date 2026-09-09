#include "protocol/FrameCodec.h"

#include <QtEndian>

#include <algorithm>

namespace ev::protocol {

QByteArray encodeFrame(QByteArrayView payload)
{
    if (payload.isEmpty() || payload.size() > MaxPayloadBytes) {
        throw FrameError("invalid payload length");   // 空包/超限包直接拒绝，防内存耗尽
    }

    QByteArray frame;
    frame.resize(4 + payload.size());
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), frame.data()); // 4字节大端长度前缀
    std::copy(payload.begin(), payload.end(), frame.begin() + 4);
    return frame;
}

// TCP 是字节流没有消息边界：攒缓冲区，攒够"4字节头+完整body"才切出一条消息，
// 一次可切多条（粘包），不够就等下次 readyRead（半包）。
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
            break;   // 半包：保留在缓冲区等待后续字节
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
