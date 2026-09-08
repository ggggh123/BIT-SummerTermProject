#include "app/UserAppConfig.h"

#include <QDir>
#include <QSettings>

namespace {
QString configValue(const char *environmentName, const QSettings &settings, const QString &iniKey) {
    if (qEnvironmentVariableIsSet(environmentName)) {
        return QString::fromLocal8Bit(qgetenv(environmentName)).trimmed();
    }
    return settings.value(iniKey).toString().trimmed();
}
} // namespace

UserAppConfig UserAppConfig::load(const QString &iniPath) {
    const QString resolvedIniPath = iniPath.isEmpty()
        ? QDir::current().absoluteFilePath(QStringLiteral("config.local.ini"))
        : iniPath;
    const QSettings settings(resolvedIniPath, QSettings::IniFormat);

    UserAppConfig config;
    config.serverHost = configValue("EV_SERVER_HOST", settings, QStringLiteral("server/host"));
    const QString portText = configValue("EV_SERVER_PORT", settings, QStringLiteral("server/port"));
    config.tencentMapKey = configValue("EV_TENCENT_MAP_KEY", settings, QStringLiteral("tencent/mapKey"));

    bool portOk = false;
    const uint parsedPort = portText.toUInt(&portOk);
    if (portOk && parsedPort > 0 && parsedPort <= 65535) {
        config.serverPort = static_cast<quint16>(parsedPort);
    }

    if (config.serverHost.isEmpty()) {
        config.validationErrors.append(QStringLiteral("缺少服务器地址（EV_SERVER_HOST 或 config.local.ini）"));
    }
    if (config.serverPort == 0) {
        config.validationErrors.append(QStringLiteral("服务器端口无效（EV_SERVER_PORT 或 config.local.ini）"));
    }
    if (config.tencentMapKey.isEmpty()) {
        config.tencentMapKey = bundledTencentMapKey();
    }
    return config;
}

QString UserAppConfig::bundledTencentMapKey() {
    // 注意：该 Key 会进入版本库。若仓库公开或 Key 配额异常，
    // 需在腾讯位置服务控制台调整配额/白名单并轮换此值。
    return QStringLiteral("II3BZ-TK5C7-NXRXH-PCEX2-XZ365-HYFIV");
}

bool UserAppConfig::isValid() const {
    return validationErrors.isEmpty();
}

QString UserAppConfig::validationMessage() const {
    return validationErrors.join(QLatin1Char('\n'));
}
