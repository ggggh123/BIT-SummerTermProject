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
    enum class NavigationTransition { OrdinaryLeave, SessionReset };

    struct MutationAuthorityStamp final {
        quint64 revisionAtDispatch = 0;
        qint64 subjectOrderId = 0;
    };

    void updateAuthenticatedNavigation();
    void showPage(QWidget *page,
                  NavigationTransition transition = NavigationTransition::OrdinaryLeave);
    void applySelectionInvalidation(quint64 selectionGeneration);
    void adoptActiveOrder(const ev::user::Order &order);
    void clearActiveOrder(qint64 resolvedOwnerOrderId, bool force = false);
    void cancelReconnectCurrent();
    void requestReconnectCurrent();
    void resolveReconnectCurrent();
    void runQueuedReconnectReads();
    void updateReconnectAuthorityUi(const QString &message = {});
    [[nodiscard]] static QString reconnectErrorText(const ev::user::ApiError &error);

    class TcpJsonClient *client_;
    class UserApi *userApi_;
    class TencentMapClient *mapClient_;
    class LoginPage *loginPage_;
    class QStackedWidget *pages_;
    class NearbyPage *nearbyPage_;
    class HistoryPage *historyPage_;
    class ProfilePage *profilePage_;
    class NavigationPage *navigationPage_ = nullptr;
    QWidget *authenticatedNavigation_;
    class QLabel *currentAuthorityStatus_;
    class QPushButton *currentAuthorityRetryButton_;
    class QPushButton *nearbyNavigationButton_;
    class QPushButton *currentOrderNavigationButton_;
    class QPushButton *historyNavigationButton_;
    class QPushButton *profileNavigationButton_;
    class ChargePage *chargePage_;
    QString mapKey_;
    bool hasActiveOrder_ = false;
    bool chargeFlowBlocked_ = false;
    bool connected_ = false;
    bool reconnectCurrentRequired_ = false;
    bool reconnectReadsQueued_ = false;
    quint64 authorityRevision_ = 0;
    std::optional<ev::user::Order> authoritativeActiveOrder_;
    std::optional<ev::user::StationSelection> rememberedSelection_;
    std::optional<ev::user::RequestContext> guardContext_;
    std::optional<ev::user::RequestContext> reconnectCurrentContext_;
    qint64 reconnectCurrentOwnerOrderId_ = 0;
    std::optional<quint64> deferredSelectionInvalidation_;
    QHash<QString, MutationAuthorityStamp> mutationAuthorityStamps_;
};
