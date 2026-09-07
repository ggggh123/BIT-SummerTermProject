#pragma once
#include <QHash>
#include <QString>

using TokenRoles = QHash<QString, QString>;
namespace RequestPreflight {
inline QString roleForToken(const TokenRoles &roles, const QString &token)
{
    const auto normalized=token.trimmed();
    if (roles.contains(normalized)) return roles.value(normalized);
    const auto configuredSimulatorToken=qEnvironmentVariable("EV_SIMULATOR_TOKEN");
    if (!configuredSimulatorToken.isEmpty()) {
        if (token==configuredSimulatorToken) return "simulator";
    } else if (normalized=="sim-token" || normalized=="simulator-token" || normalized=="demo-simulator-token") {
        return "simulator";
    }
    if (normalized=="ml-token" || normalized=="forecast-token" || normalized=="demo-ml-token") return "ml";
    return {};
}
} // namespace RequestPreflight
