#pragma once

#include <QByteArray>
#include <QString>

namespace WebEngineRuntime {

[[nodiscard]] QByteArray chromiumFlagsForSystem(
    const QByteArray &existingFlags,
    const QByteArray &systemVendor);

void applySystemCompatibility(
    const QString &systemVendorPath = QStringLiteral("/sys/class/dmi/id/sys_vendor"));

} // namespace WebEngineRuntime
