#include "app/AppContext.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChargingPlatformServer"));
    QApplication::setOrganizationName(QStringLiteral("NeusoftTraining"));

    AppContext context;
    const Result initResult = context.initialize();
    if (!initResult.ok) {
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"), initResult.message);
        return 1;
    }

    LoginDialog login(context.authService());
    if (login.exec() != QDialog::Accepted) {
        return 0;
    }

    MainWindow window(&context);
    window.show();
    return app.exec();
}

