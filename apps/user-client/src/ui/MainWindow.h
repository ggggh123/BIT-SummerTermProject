#pragma once

#include "app/UserAppConfig.h"

#include <QMainWindow>

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(UserAppConfig config, QWidget *parent = nullptr);

private:
    class TcpJsonClient *client_;
    class UserApi *userApi_;
    class LoginPage *loginPage_;
    class QStackedWidget *pages_;
    QWidget *nearbyPage_;
    QWidget *chargePage_;
};
