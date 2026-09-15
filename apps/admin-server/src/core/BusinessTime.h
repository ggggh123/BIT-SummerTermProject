#pragma once

#include <QDateTime>
#include <QRegularExpression>

namespace BusinessTime {

// 合同固定 +08:00、四位年份。只验证整数日期/时间，不让日期解析器
// 截断或拒绝任意长度小数；比较键的小数仅去掉末尾零。
inline QString timestampKey(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral(
        "\\A([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\\.([0-9]+))?\\+08:00\\z"));
    const auto match = pattern.match(value);
    if (!match.hasMatch()
        || !QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt()).isValid()
        || !QTime(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt()).isValid()) {
        return {};
    }
    QString fraction = match.captured(7);
    while (fraction.endsWith(QLatin1Char('0'))) fraction.chop(1);
    return value.left(19) + QLatin1Char('.') + fraction;
}

inline QString now()
{
    return QDateTime::currentDateTimeUtc().toOffsetFromUtc(8 * 3600).toString(Qt::ISODateWithMs);
}

inline qint64 elapsed(const QString &startedAt, const QString &endedAt)
{
    const QDateTime start = QDateTime::fromString(startedAt, Qt::ISODate);
    const QDateTime end = endedAt.isEmpty() ? QDateTime::currentDateTimeUtc()
                                         : QDateTime::fromString(endedAt, Qt::ISODate);
    return start.isValid() && end.isValid() ? qMax<qint64>(0, start.secsTo(end)) : 0;
}

} // namespace BusinessTime
