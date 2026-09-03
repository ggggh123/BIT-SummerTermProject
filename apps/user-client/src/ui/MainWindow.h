#pragma once

#include "app/UserAppConfig.h"
#include "domain/Models.h"

#include <QMainWindow>
#include <QHash>

#include <optional>

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(UserAppConfig config, QWidget *parent = nullptr);

private:
    struct MutationAuthorityStamp final {
        quint64 revisionAtDispatch = 0;
        qint64 subjectOrderId = 0;
    };

    void updateAuthenticatedNavigation();
    void applySelectionInvalidation(quint64 selectionGeneration);
    void adoptActiveOrder(const ev::user::Order &order);
    void clearActiveOrder(qint64 resolvedOwnerOrderId, bool force = false);

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
    quint64 authorityRevision_ = 0;
    std::optional<ev::user::Order> authoritativeActiveOrder_;
    std::optional<ev::user::StationSelection> rememberedSelection_;
    std::optional<ev::user::RequestContext> guardContext_;
    std::optional<quint64> deferredSelectionInvalidation_;
    QHash<QString, MutationAuthorityStamp> mutationAuthorityStamps_;
};
