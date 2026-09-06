#include "app/WebEngineRuntime.h"

#include <QFile>

#include <cctype>

namespace {
constexpr auto kChromiumFlagsEnvironment = "QTWEBENGINE_CHROMIUM_FLAGS";
constexpr auto kIgnoreGpuBlocklistFlag = "--ignore-gpu-blocklist";

bool isVmwareVendor(const QByteArray &systemVendor) {
    return systemVendor.trimmed().toLower() == QByteArrayLiteral("vmware, inc.");
}

bool containsExactToken(const QByteArray &flags, const QByteArray &token) {
    return flags.simplified().split(' ').contains(token);
}
} // namespace

QByteArray WebEngineRuntime::chromiumFlagsForSystem(
    const QByteArray &existingFlags,
    const QByteArray &systemVendor) {
    const QByteArray requiredFlag(kIgnoreGpuBlocklistFlag);
    if (!isVmwareVendor(systemVendor) || containsExactToken(existingFlags, requiredFlag)) {
        return existingFlags;
    }

    QByteArray updatedFlags = existingFlags;
    if (!updatedFlags.isEmpty()
        && !std::isspace(static_cast<unsigned char>(updatedFlags.back()))) {
        updatedFlags.append(' ');
    }
    updatedFlags.append(requiredFlag);
    return updatedFlags;
}

void WebEngineRuntime::applySystemCompatibility(const QString &systemVendorPath) {
    QFile systemVendorFile(systemVendorPath);
    if (!systemVendorFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QByteArray systemVendor = systemVendorFile.readAll();
    if (systemVendorFile.error() != QFileDevice::NoError) {
        return;
    }

    const QByteArray existingFlags = qgetenv(kChromiumFlagsEnvironment);
    const QByteArray updatedFlags = chromiumFlagsForSystem(existingFlags, systemVendor);
    if (updatedFlags != existingFlags) {
        qputenv(kChromiumFlagsEnvironment, updatedFlags);
    }
}
