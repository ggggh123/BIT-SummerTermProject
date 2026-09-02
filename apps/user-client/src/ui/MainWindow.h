#pragma once

#include "app/UserAppConfig.h"
#include "domain/Models.h"

#include <QMainWindow>

#include <optional>

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(UserAppConfig config, QWidget *parent = nullptr);

private:
    void updateAuthenticatedNavigation();

    class TcpJsonClient *client_;
    class UserApi *userApi_;
    class TencentMapClient *mapClient_;
    class LoginPage *loginPage_;
    class QStackedWidget *pages_;
    class NearbyPage *nearbyPage_;
    class ProfilePage *profilePage_;
    class NavigationPage *navigationPage_ = nullptr;
    QWidget *authenticatedNavigation_;
    class QPushButton *nearbyNavigationButton_;
    class QPushButton *currentOrderNavigationButton_;
    class QPushButton *profileNavigationButton_;
    class ChargePage *chargePage_;
    QString mapKey_;
    bool hasActiveOrder_ = false;
    bool chargeFlowBlocked_ = false;
    std::optional<ev::user::Order> authoritativeActiveOrder_;
    std::optional<ev::user::StationSelection> rememberedSelection_;
    std::optional<ev::user::RequestContext> guardContext_;
};
