#pragma once

#include <QRect>
#include <QSize>

class QApplication;

namespace UiTheme {

void apply(QApplication &application);
[[nodiscard]] QSize initialWindowSize(const QRect &availableGeometry);

} // namespace UiTheme
