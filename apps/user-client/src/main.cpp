#include "app/UserAppConfig.h"
#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    MainWindow window(UserAppConfig::load());
    window.resize(720, 480);
    window.show();
    return application.exec();
}
