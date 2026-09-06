#pragma once

#include <QDateTime>

namespace BusinessTime {

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
