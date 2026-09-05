#include "domain/ContractTimestamp.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

#include <algorithm>

namespace {

struct TimestampParts final {
    QDateTime wholeSecond;
    QStringView fraction;
};

std::optional<int> decimalField(QStringView value, qsizetype begin, qsizetype length)
{
    int result = 0;
    for (qsizetype index = begin; index < begin + length; ++index) {
        const QChar character = value.at(index);
        if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
            return std::nullopt;
        }
        result = result * 10 + character.digitValue();
    }
    return result;
}

std::optional<TimestampParts> timestampParts(QStringView value)
{
    constexpr qsizetype kWholeSecondLength = 19;
    constexpr qsizetype kOffsetLength = 6;
    constexpr qsizetype kWithoutFractionLength =
        kWholeSecondLength + kOffsetLength;
    if (value.size() < kWithoutFractionLength
        || value.at(4) != QLatin1Char('-')
        || value.at(7) != QLatin1Char('-')
        || value.at(10) != QLatin1Char('T')
        || value.at(13) != QLatin1Char(':')
        || value.at(16) != QLatin1Char(':')) {
        return std::nullopt;
    }

    QStringView fraction;
    qsizetype offsetBegin = kWholeSecondLength;
    if (value.size() == kWithoutFractionLength) {
        if (value.at(kWholeSecondLength) != QLatin1Char('+')) {
            return std::nullopt;
        }
    } else {
        if (value.at(kWholeSecondLength) != QLatin1Char('.')) {
            return std::nullopt;
        }
        offsetBegin = value.size() - kOffsetLength;
        if (offsetBegin <= kWholeSecondLength + 1) {
            return std::nullopt;
        }
        fraction = value.sliced(kWholeSecondLength + 1,
                                offsetBegin - kWholeSecondLength - 1);
        for (const QChar digit : fraction) {
            if (digit < QLatin1Char('0') || digit > QLatin1Char('9')) {
                return std::nullopt;
            }
        }
    }
    if (value.sliced(offsetBegin) != QStringView(u"+08:00")) {
        return std::nullopt;
    }

    const auto year = decimalField(value, 0, 4);
    const auto month = decimalField(value, 5, 2);
    const auto day = decimalField(value, 8, 2);
    const auto hour = decimalField(value, 11, 2);
    const auto minute = decimalField(value, 14, 2);
    const auto second = decimalField(value, 17, 2);
    if (!year.has_value() || !month.has_value() || !day.has_value()
        || !hour.has_value() || !minute.has_value() || !second.has_value()) {
        return std::nullopt;
    }
    const QDate date(*year, *month, *day);
    const QTime time(*hour, *minute, *second);
    if (!date.isValid() || !time.isValid()) {
        return std::nullopt;
    }
    const QDateTime wholeSecond(date, time, QTimeZone(8 * 60 * 60));
    if (!wholeSecond.isValid()) {
        return std::nullopt;
    }
    return TimestampParts{wholeSecond, fraction};
}

} // namespace

namespace ev::user {

std::optional<TimestampComparison> compareContractTimestamps(
    QStringView left, QStringView right, qint64 rightSecondOffset)
{
    const auto leftParts = timestampParts(left);
    const auto rightParts = timestampParts(right);
    if (!leftParts.has_value() || !rightParts.has_value()) {
        return std::nullopt;
    }
    const QDateTime adjustedRight = rightParts->wholeSecond.addSecs(rightSecondOffset);
    if (!adjustedRight.isValid()) {
        return std::nullopt;
    }
    if (leftParts->wholeSecond < adjustedRight) {
        return TimestampComparison::Earlier;
    }
    if (leftParts->wholeSecond > adjustedRight) {
        return TimestampComparison::Later;
    }

    const qsizetype digits = std::max(leftParts->fraction.size(),
                                      rightParts->fraction.size());
    for (qsizetype index = 0; index < digits; ++index) {
        const QChar leftDigit = index < leftParts->fraction.size()
            ? leftParts->fraction.at(index) : QLatin1Char('0');
        const QChar rightDigit = index < rightParts->fraction.size()
            ? rightParts->fraction.at(index) : QLatin1Char('0');
        if (leftDigit < rightDigit) {
            return TimestampComparison::Earlier;
        }
        if (leftDigit > rightDigit) {
            return TimestampComparison::Later;
        }
    }
    return TimestampComparison::Equal;
}

} // namespace ev::user
