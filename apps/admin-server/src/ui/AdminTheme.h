#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace AdminTheme {

void apply(QApplication &application);
QIcon icon(const QString &name, const QColor &color = QColor("#718078"));

} // namespace AdminTheme
