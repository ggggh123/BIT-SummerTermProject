#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace ev::reset {

enum class ReceiptState {
    Pending,
    Final
};

enum class NextStep {
    BeginCoreReset,
    ResumeSnapshot,
    ReplayFinalAck
};

struct Receipt {
    ReceiptState state;
    QString requestId;
    QString resetAt;
    QString goldenHash;
    qint64 snapshotVersion;
    QByteArray finalAck;
};

inline Receipt makePendingReceipt(QString requestId,
                                  QString resetAt,
                                  QString goldenHash,
                                  qint64 snapshotVersion)
{
    return {
        ReceiptState::Pending,
        std::move(requestId),
        std::move(resetAt),
        std::move(goldenHash),
        snapshotVersion,
        {}
    };
}

inline Receipt finalizeReceipt(Receipt receipt, QByteArray finalAck)
{
    receipt.state = ReceiptState::Final;
    receipt.finalAck = std::move(finalAck);
    return receipt;
}

inline NextStep nextStep(const Receipt &receipt)
{
    return receipt.state == ReceiptState::Pending
        ? NextStep::ResumeSnapshot
        : NextStep::ReplayFinalAck;
}

inline NextStep nextStep(const std::optional<Receipt> &receipt)
{
    return receipt.has_value() ? nextStep(*receipt) : NextStep::BeginCoreReset;
}

} // namespace ev::reset
