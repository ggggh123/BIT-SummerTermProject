#include "domain/Formatters.h"

#include <QRegularExpression>
#include <QtMath>

bool isValidPhone(const QString &phone) {
    static const QRegularExpression pattern(QStringLiteral("^1[3-9][0-9]{9}$"));
    return pattern.match(phone).hasMatch();
}

std::optional<qint64> parsePositiveFen(const QString &amount) {
    static const QRegularExpression pattern(QStringLiteral("^[1-9][0-9]*(\\.[0-9]{1,2})?$"));
    if (!pattern.match(amount).hasMatch()) {
        return std::nullopt;
    }

    const QStringList parts = amount.split(QLatin1Char('.'));
    const QString cents = parts.value(0) + parts.value(1).leftJustified(2, QLatin1Char('0'));
    bool conversionOk = false;
    const qint64 fen = cents.toLongLong(&conversionOk);
    if (!conversionOk || fen <= 0) {
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
