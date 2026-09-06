#pragma once

#include <QStringView>
#include <QtGlobal>

#include <optional>

namespace ev::user {

enum class TimestampComparison {
    Earlier = -1,
    Equal = 0,
    Later = 1,
};

// Compares left with right plus an optional whole-second offset. A null result
// means at least one value is not a valid contract Timestamp.
[[nodiscard]] std::optional<TimestampComparison> compareContractTimestamps(
    QStringView left, QStringView right, qint64 rightSecondOffset = 0);

} // namespace ev::user
