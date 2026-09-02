#pragma once

#include "app/UserAppConfig.h"

#include <QMainWindow>

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(UserAppConfig config, QWidget *parent = nullptr);

private:
    class TcpJsonClient *client_;
    class UserApi *userApi_;
    class TencentMapClient *mapClient_;
    class LoginPage *loginPage_;
    class QStackedWidget *pages_;
    class NearbyPage *nearbyPage_;
    class NavigationPage *navigationPage_ = nullptr;
    QWidget *chargePage_;
    QString mapKey_;
};
