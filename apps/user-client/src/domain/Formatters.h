#pragma once

#include <QString>
#include <optional>

bool isValidPhone(const QString &phone);
std::optional<qint64> parsePositiveFen(const QString &amount);
QString formatFen(qint64 fen);
double haversineKm(double latitudeA, double longitudeA, double latitudeB, double longitudeB);
