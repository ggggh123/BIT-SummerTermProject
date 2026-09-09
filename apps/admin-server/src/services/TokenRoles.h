#pragma once
#include <QHash>
#include <QMetaType>
#include <QString>
#include <utility>

// token → 角色映射：登录会话在内存中登记；模拟器/ML 用配置的固定 token 识别。
class TokenRoles
{
public:
    TokenRoles() = default;
    static TokenRoles fromEnvironment()
    {
        return TokenRoles(qEnvironmentVariable("EV_SIMULATOR_TOKEN"));
    }

    bool contains(const QString &token) const { return m_roles.contains(token); }
    QString value(const QString &token) const { return m_roles.value(token); }
    void insert(const QString &token, const QString &role) { m_roles.insert(token,role); }
    void clear() { m_roles.clear(); }
    const QString &configuredSimulatorToken() const { return m_configuredSimulatorToken; }

private:
    explicit TokenRoles(QString configuredSimulatorToken)
        : m_configuredSimulatorToken(std::move(configuredSimulatorToken))
    {
    }

    QHash<QString, QString> m_roles;
    QString m_configuredSimulatorToken;
};

Q_DECLARE_METATYPE(TokenRoles)

namespace RequestPreflight {
inline QString roleForToken(const TokenRoles &roles, const QString &token)
{
    const auto normalized=token.trimmed();
    if (roles.contains(normalized)) return roles.value(normalized);
    const auto &configuredSimulatorToken=roles.configuredSimulatorToken();
    if (!configuredSimulatorToken.isEmpty()) {
        if (token==configuredSimulatorToken) return "simulator";
    } else if (normalized=="sim-token" || normalized=="simulator-token" || normalized=="demo-simulator-token") {
        return "simulator";
    }
    if (normalized=="ml-token" || normalized=="forecast-token" || normalized=="demo-ml-token") return "ml";
    return {};
}
} // namespace RequestPreflight
