#include "protocol/FrameCodec.h"

#include <QtTest/QtTest>

class FrameCodecTest : public QObject
{
    Q_OBJECT

private slots:
    void decodesFragmentedAndCoalescedFrames();
    void rejectsInvalidLengths();
};

void FrameCodecTest::decodesFragmentedAndCoalescedFrames()
{
    ev::protocol::FrameDecoder decoder;
    const QByteArray firstPayload = R"({"a":1})";
    const QByteArray secondPayload = R"({"b":2})";
    const QByteArray a = ev::protocol::encodeFrame(firstPayload);
    const QByteArray b = ev::protocol::encodeFrame(secondPayload);

    QCOMPARE(decoder.append(QByteArrayView(a).left(2)).size(), 0);
    QCOMPARE(decoder.append(a.mid(2) + b),
             QList<QByteArray>({firstPayload, secondPayload}));
}

void FrameCodecTest::rejectsInvalidLengths()
{
    ev::protocol::FrameDecoder decoder;
    QVERIFY_EXCEPTION_THROWN(decoder.append(QByteArray::fromHex("00000000")),
                             ev::protocol::FrameError);
    decoder.reset();
    QVERIFY_EXCEPTION_THROWN(decoder.append(QByteArray::fromHex("00100001")),
                             ev::protocol::FrameError);
}

QTEST_MAIN(FrameCodecTest)

#include "tst_framecodec.moc"
