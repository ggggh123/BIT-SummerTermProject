#include "app/UserAppConfig.h"
#include "app/WebEngineRuntime.h"
#include "ui/MainWindow.h"
#include "ui/UiTheme.h"

#include <QApplication>
#include <QScreen>

int main(int argc, char *argv[]) {
    WebEngineRuntime::applySystemCompatibility();
    QApplication application(argc, argv);
    UiTheme::apply(application);
    MainWindow window(UserAppConfig::load());
    const QRect availableGeometry = application.primaryScreen() == nullptr
        ? QRect(0, 0, 390, 844)
        : application.primaryScreen()->availableGeometry();
    window.resize(UiTheme::initialWindowSize(availableGeometry));
    window.show();
    return application.exec();
}
