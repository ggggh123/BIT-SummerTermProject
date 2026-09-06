#pragma once
#include <QHash>
#include <QString>

using TokenRoles = QHash<QString, QString>;
namespace RequestPreflight {
inline QString roleForToken(const TokenRoles &roles, const QString &token)
{
    const auto normalized=token.trimmed();
    if (roles.contains(normalized)) return roles.value(normalized);
    if (normalized=="sim-token" || normalized=="simulator-token" || normalized=="demo-simulator-token") return "simulator";
    if (normalized=="ml-token" || normalized=="forecast-token" || normalized=="demo-ml-token") return "ml";
    return {};
}
} // namespace RequestPreflight
