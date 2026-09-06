#include "domain/Formatters.h"

#include <QRegularExpression>
#include <QtMath>

bool isValidPhone(const QString &phone) {
    static const QRegularExpression pattern(QStringLiteral("^1[3-9][0-9]{9}$"));
    return pattern.match(phone).hasMatch();
}

std::optional<qint64> parsePositiveFen(const QString &amount) {
    constexpr qint64 maxSafeInteger = 9'007'199'254'740'991LL;
    static const QRegularExpression pattern(
        QStringLiteral("\\A(?:0|[1-9][0-9]*)(?:\\.[0-9]{1,2})?\\z"));
    if (!pattern.match(amount).hasMatch()) {
        return std::nullopt;
    }

    const QStringList parts = amount.split(QLatin1Char('.'));
    qint64 major = 0;
    for (const QChar digit : parts.constFirst()) {
        const qint64 value = digit.unicode() - QLatin1Char('0').unicode();
        if (major > (maxSafeInteger / 100 - value) / 10) {
            return std::nullopt;
        }
        major = major * 10 + value;
    }

    const QString fraction = parts.value(1).leftJustified(2, QLatin1Char('0'));
    qint64 minor = 0;
    for (const QChar digit : fraction) {
        minor = minor * 10 + digit.unicode() - QLatin1Char('0').unicode();
    }
    if (major > (maxSafeInteger - minor) / 100) {
        return std::nullopt;
    }
    const qint64 fen = major * 100 + minor;
    if (fen <= 0) {
        return std::nullopt;
    }
    return fen;
}

QString formatFen(qint64 fen) {
    const bool negative = fen < 0;
    QString major = QString::number(fen / 100);
    if (negative && major.startsWith(QLatin1Char('-'))) {
        major.remove(0, 1);
    }
    qint64 minor = fen % 100;
    if (minor < 0) {
        minor = -minor;
    }
    return QStringLiteral("%1%2.%3")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(major)
        .arg(minor, 2, 10, QLatin1Char('0'));
}

double haversineKm(double latitudeA, double longitudeA, double latitudeB, double longitudeB) {
    constexpr double earthRadiusKm = 6371.0;
    const double latitudeDelta = qDegreesToRadians(latitudeB - latitudeA);
    const double longitudeDelta = qDegreesToRadians(longitudeB - longitudeA);
    const double latitudeARadians = qDegreesToRadians(latitudeA);
    const double latitudeBRadians = qDegreesToRadians(latitudeB);
    const double haversine = qPow(qSin(latitudeDelta / 2.0), 2)
        + qCos(latitudeARadians) * qCos(latitudeBRadians) * qPow(qSin(longitudeDelta / 2.0), 2);
    return earthRadiusKm * 2.0 * qAtan2(qSqrt(haversine), qSqrt(1.0 - haversine));
}
