#include "ui/MainWindow.h"

#include "net/TcpJsonClient.h"
#include "net/TencentMapClient.h"
#include "services/UserApi.h"
#include "ui/ChargePage.h"
#include "ui/HistoryPage.h"
#include "ui/LoginPage.h"
#include "ui/NavigationPage.h"
#include "ui/NearbyPage.h"
#include "ui/ProfilePage.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionButton>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {

class ResponsiveStackedWidget final : public QStackedWidget
{
public:
    using QStackedWidget::QStackedWidget;

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        const QWidget *current = currentWidget();
        const int height = current == nullptr ? 0 : current->minimumSizeHint().height();
        return {0, height};
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const QWidget *current = currentWidget();
        if (current == nullptr) {
            return {390, 720};
        }
        QSize hint = current->sizeHint();
        hint.setWidth(qMin(390, hint.width()));
        return hint;
    }
};

class MobileTabButton final : public QPushButton
{
public:
    MobileTabButton(QString text, const QString &iconPath, QWidget *parent)
        : QPushButton(std::move(text), parent)
    {
        setIcon(QIcon(iconPath));
        setIconSize(QSize(22, 22));
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        QSize hint = QPushButton::sizeHint();
        hint.setHeight(qMax(64, hint.height()));
        return hint;
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        QSize hint = QPushButton::minimumSizeHint();
        hint.setHeight(qMax(56, hint.height()));
        return hint;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionButton option;
        initStyleOption(&option);
        const QString label = option.text;
        const QIcon tabIcon = option.icon;
        option.text.clear();
        option.icon = {};

        QPainter painter(this);
        style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
        const QRect content = style()->subElementRect(
            QStyle::SE_PushButtonContents, &option, this);
        const int iconSide = qMin(22, qMax(0, content.height() - 24));
        const QRect iconRect(content.center().x() - iconSide / 2,
                             content.top() + 2, iconSide, iconSide);
        const QIcon::Mode mode = isEnabled()
            ? ((option.state & QStyle::State_MouseOver) ? QIcon::Active : QIcon::Normal)
            : QIcon::Disabled;
        const QColor textColor = option.palette.color(
            isEnabled() ? QPalette::Active : QPalette::Disabled,
            QPalette::ButtonText);
        QPixmap iconPixmap(iconRect.size() * devicePixelRatioF());
        iconPixmap.setDevicePixelRatio(devicePixelRatioF());
        iconPixmap.fill(Qt::transparent);
        {
            QPainter iconPainter(&iconPixmap);
            const QRect logicalPixmapRect(QPoint(), iconRect.size());
            tabIcon.paint(&iconPainter, logicalPixmapRect, Qt::AlignCenter, mode);
            iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            iconPainter.fillRect(logicalPixmapRect, textColor);
        }
        painter.drawPixmap(iconRect.topLeft(), iconPixmap);

        painter.setPen(textColor);
        const QRect textRect(content.left(), iconRect.bottom() + 1,
                             content.width(),
                             qMax(0, content.bottom() - iconRect.bottom()));
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine,
                         label);
    }
};

void setSelected(QPushButton *button, bool selected)
{
    if (button->property("selected").toBool() == selected) {
        return;
    }
    button->setProperty("selected", selected);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

} // namespace

MainWindow::MainWindow(UserAppConfig config, QWidget *parent)
    : QMainWindow(parent)
    , client_(new TcpJsonClient(this))
    , userApi_(new UserApi(client_, this))
    , mapClient_(new TencentMapClient(config.tencentMapKey, nullptr,
                                      TencentMapClient::productionEndpoint(), 5'000, this))
    , mapKey_(config.tencentMapKey) {
    setWindowTitle(QStringLiteral("电动汽车充电"));

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *configurationMessage = new QLabel(centralWidget);
    configurationMessage->setObjectName(QStringLiteral("configurationMessage"));
    configurationMessage->setWordWrap(true);
    if (config.isValid()) {
        configurationMessage->setText(QStringLiteral("配置已就绪"));
        configurationMessage->hide();
    } else {
        configurationMessage->setText(QStringLiteral("配置错误：\n%1").arg(config.validationMessage()));
        configurationMessage->setProperty("role", QStringLiteral("danger"));
        configurationMessage->setContentsMargins(20, 12, 20, 12);
    }
    layout->addWidget(configurationMessage);

    currentAuthorityStatus_ = new QLabel(centralWidget);
    currentAuthorityStatus_->setObjectName(QStringLiteral("currentAuthorityStatus"));
    currentAuthorityStatus_->setWordWrap(true);
    currentAuthorityStatus_->setProperty("role", QStringLiteral("danger"));
    currentAuthorityStatus_->setContentsMargins(20, 8, 20, 4);
    currentAuthorityStatus_->hide();
    currentAuthorityRetryButton_ = new QPushButton(
        QStringLiteral("重试订单校验"), centralWidget);
    currentAuthorityRetryButton_->setObjectName(
        QStringLiteral("currentAuthorityRetryButton"));
    currentAuthorityRetryButton_->setProperty("role", QStringLiteral("danger"));
    currentAuthorityRetryButton_->hide();
    layout->addWidget(currentAuthorityStatus_);
    layout->addWidget(currentAuthorityRetryButton_);

    authenticatedNavigation_ = new QWidget(centralWidget);
    authenticatedNavigation_->setObjectName(QStringLiteral("authenticatedNavigation"));
    auto *navigationLayout = new QHBoxLayout(authenticatedNavigation_);
    navigationLayout->setContentsMargins(4, 4, 4, 4);
    navigationLayout->setSpacing(4);
    nearbyNavigationButton_ = new MobileTabButton(
        QStringLiteral("找桩"), QStringLiteral(":/ui/location.svg"),
        authenticatedNavigation_);
    nearbyNavigationButton_->setObjectName(QStringLiteral("nearbyNavigationButton"));
    currentOrderNavigationButton_ = new MobileTabButton(
        QStringLiteral("当前订单"), QStringLiteral(":/ui/battery-charging.svg"),
        authenticatedNavigation_);
    currentOrderNavigationButton_->setObjectName(QStringLiteral("currentOrderNavigationButton"));
    currentOrderNavigationButton_->setVisible(false);
    historyNavigationButton_ = new MobileTabButton(
        QStringLiteral("历史订单"), QStringLiteral(":/ui/history.svg"),
        authenticatedNavigation_);
    historyNavigationButton_->setObjectName(QStringLiteral("historyNavigationButton"));
    profileNavigationButton_ = new MobileTabButton(
        QStringLiteral("我的账户"), QStringLiteral(":/ui/person.svg"),
        authenticatedNavigation_);
    profileNavigationButton_->setObjectName(QStringLiteral("profileNavigationButton"));
    const QList<QPushButton *> navigationButtons{
        nearbyNavigationButton_, currentOrderNavigationButton_,
        historyNavigationButton_, profileNavigationButton_};
    for (QPushButton *button : navigationButtons) {
        button->setProperty("role", QStringLiteral("tab"));
        button->setProperty("selected", false);
        navigationLayout->addWidget(button, 1);
    }
    authenticatedNavigation_->setVisible(false);

    auto *contentViewport = new QScrollArea(centralWidget);
    contentViewport->setObjectName(QStringLiteral("contentViewport"));
    contentViewport->setWidgetResizable(true);
    contentViewport->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentViewport->setFrameShape(QFrame::NoFrame);

    pages_ = new ResponsiveStackedWidget;
    pages_->setObjectName(QStringLiteral("mainPages"));
    pages_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    loginPage_ = new LoginPage(pages_);
    nearbyPage_ = new NearbyPage(userApi_, mapClient_, pages_);
    historyPage_ = new HistoryPage(userApi_, pages_);
    profilePage_ = new ProfilePage(userApi_, pages_);
    chargePage_ = new ChargePage(userApi_, pages_);
    pages_->addWidget(loginPage_);
    pages_->addWidget(nearbyPage_);
    pages_->addWidget(historyPage_);
    pages_->addWidget(profilePage_);
    pages_->addWidget(chargePage_);
    showPage(loginPage_);
    contentViewport->setWidget(pages_);
    layout->addWidget(contentViewport, 1);
    layout->addWidget(authenticatedNavigation_);

    connect(loginPage_, &LoginPage::loginRequested, userApi_, &UserApi::loginByPhone);
    connect(userApi_, &UserApi::loginPendingChanged, loginPage_, &LoginPage::setPending);
    connect(userApi_, &UserApi::connectionChanged, loginPage_, &LoginPage::setConnectionAvailable);
    connect(userApi_, &UserApi::connectionChanged, nearbyPage_, &NearbyPage::setConnectionAvailable);
    connect(userApi_, &UserApi::requestFailed, this, [this](const ev::user::ApiError &error) {
        if (pages_->currentWidget() == loginPage_) {
            loginPage_->setError(error.message);
        }
    });
    connect(userApi_, &UserApi::sessionReset, this, [this](quint64 sessionGeneration) {
        guardContext_.reset();
        reconnectCurrentContext_.reset();
        reconnectCurrentOwnerOrderId_ = 0;
        reconnectCurrentRequired_ = false;
        reconnectReadsQueued_ = false;
        deferredSelectionInvalidation_.reset();
        mutationAuthorityStamps_.clear();
        rememberedSelection_.reset();
        authoritativeActiveOrder_.reset();
        hasActiveOrder_ = false;
        chargeFlowBlocked_ = false;
        ++authorityRevision_;
        nearbyPage_->resetForSessionExpiry();
        historyPage_->resetForSessionExpiry(sessionGeneration);
        profilePage_->resetForSessionExpiry();
        chargePage_->resetForSessionExpiry(sessionGeneration);
        currentAuthorityStatus_->hide();
        currentAuthorityRetryButton_->hide();
        currentOrderNavigationButton_->hide();
        authenticatedNavigation_->setVisible(false);
        pages_->setEnabled(true);
        showPage(loginPage_, NavigationTransition::SessionReset);
        loginPage_->setPending(false);
        loginPage_->setError({});
    });
    connect(userApi_, &UserApi::sessionExpired, this, [this](quint64) {
        loginPage_->setError(QStringLiteral("登录已失效，请重新登录"));
    });
    connect(userApi_, &UserApi::loginSucceeded, this, [this](const ev::user::User &) {
        rememberedSelection_.reset();
        deferredSelectionInvalidation_.reset();
        mutationAuthorityStamps_.clear();
        reconnectCurrentContext_.reset();
        reconnectCurrentOwnerOrderId_ = 0;
        reconnectCurrentRequired_ = false;
        reconnectReadsQueued_ = false;
        authoritativeActiveOrder_.reset();
        hasActiveOrder_ = false;
        ++authorityRevision_;
        updateReconnectAuthorityUi();
        updateAuthenticatedNavigation();
        guardContext_ = userApi_->loadCurrentOrder(
            0, 0, ev::user::ChargeOperation::Guard);
    });
    connect(userApi_, &UserApi::currentOrderLoaded, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::CurrentOrderResult &result) {
        if (!guardContext_.has_value() || context != *guardContext_) {
            return;
        }
        guardContext_.reset();
        authenticatedNavigation_->setVisible(true);
        if (result.order.has_value()) {
            std::optional<ev::user::StationSelection> matching;
            if (rememberedSelection_.has_value()
                && rememberedSelection_->station.stationId == result.order->stationId
                && rememberedSelection_->charger.chargerId == result.order->chargerId) {
                matching = rememberedSelection_;
            } else {
                rememberedSelection_.reset();
            }
            chargePage_->enterGuardOrder(*result.order, matching);
        }
        if (result.order.has_value()) {
            showPage(chargePage_);
        } else if (pages_->currentWidget() != chargePage_) {
            showPage(nearbyPage_);
        }
    });
    connect(userApi_, &UserApi::chargeRequestFailed, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::ApiError &, bool) {
        if (!guardContext_.has_value() || context != *guardContext_) {
            return;
        }
        guardContext_.reset();
        clearActiveOrder(0, true);
        authenticatedNavigation_->setVisible(false);
        showPage(loginPage_);
        loginPage_->setError(QStringLiteral("当前订单加载失败，请重新登录"));
    });
    connect(nearbyNavigationButton_, &QPushButton::clicked, this, [this] {
        if (!hasActiveOrder_ && !chargeFlowBlocked_ && !reconnectCurrentRequired_) {
            if (pages_->currentWidget() == historyPage_) {
                historyPage_->deactivate();
            }
            if (pages_->currentWidget() == chargePage_) {
                chargePage_->leavePage();
            }
            showPage(nearbyPage_);
        }
    });
    connect(currentOrderNavigationButton_, &QPushButton::clicked, this, [this] {
        if (hasActiveOrder_ && !chargeFlowBlocked_ && !reconnectCurrentRequired_
            && authoritativeActiveOrder_.has_value()) {
            cancelReconnectCurrent();
            if (pages_->currentWidget() == historyPage_) {
                historyPage_->deactivate();
            }
            std::optional<ev::user::StationSelection> matching;
            if (rememberedSelection_.has_value()
                && rememberedSelection_->station.stationId == authoritativeActiveOrder_->stationId
                && rememberedSelection_->charger.chargerId == authoritativeActiveOrder_->chargerId) {
                matching = rememberedSelection_;
            }
            chargePage_->enterOrder(*authoritativeActiveOrder_, matching);
            chargePage_->resume();
            showPage(chargePage_);
        }
    });
    connect(historyNavigationButton_, &QPushButton::clicked, this, [this] {
        if (chargeFlowBlocked_ || reconnectCurrentRequired_) {
            return;
        }
        if (pages_->currentWidget() == chargePage_) {
            chargePage_->leavePage();
        }
        showPage(historyPage_);
        historyPage_->activate();
    });
    connect(profileNavigationButton_, &QPushButton::clicked, this, [this] {
        if (chargeFlowBlocked_ || reconnectCurrentRequired_) {
            return;
        }
        if (pages_->currentWidget() == chargePage_) {
            chargePage_->leavePage();
        }
        if (pages_->currentWidget() == historyPage_) {
            historyPage_->deactivate();
        }
        showPage(profilePage_);
        profilePage_->refresh();
    });
    connect(nearbyPage_, &NearbyPage::chargerSelected, this,
            [this](const ev::user::StationSelection &selection) {
        if (hasActiveOrder_ || chargeFlowBlocked_ || reconnectCurrentRequired_) {
            return;
        }
        rememberedSelection_ = selection;
        if (pages_->currentWidget() == historyPage_) {
            historyPage_->deactivate();
        }
        chargePage_->enterSelection(selection);
        showPage(chargePage_);
    });
    connect(nearbyPage_, &NearbyPage::selectionInvalidated, this,
            [this](quint64 selectionGeneration) {
        if (chargeFlowBlocked_) {
            if (!deferredSelectionInvalidation_.has_value()
                || selectionGeneration > *deferredSelectionInvalidation_) {
                deferredSelectionInvalidation_ = selectionGeneration;
            }
            return;
        }
        applySelectionInvalidation(selectionGeneration);
    });
    connect(chargePage_, &ChargePage::nearbyRefreshRequested,
            nearbyPage_, &NearbyPage::refreshAfterCharge);
    connect(chargePage_, &ChargePage::nearbyDetailRefreshReady,
            nearbyPage_, &NearbyPage::applyChargeStationDetail);
    connect(chargePage_, &ChargePage::nearbyDetailRefreshFailed,
            nearbyPage_, &NearbyPage::failChargeStationDetail);
    connect(nearbyPage_, &NearbyPage::chargeRefreshCommitted,
            chargePage_, &ChargePage::nearbyRefreshCommitted);
    connect(nearbyPage_, &NearbyPage::chargeRefreshFailed,
            chargePage_, &ChargePage::nearbyRefreshFailed);
    connect(nearbyPage_, &NearbyPage::chargeRefreshUnavailable,
            chargePage_, &ChargePage::nearbyRefreshUnavailable);
    connect(chargePage_, &ChargePage::rememberedSelectionInvalidated, this, [this] {
        rememberedSelection_.reset();
    });
    chargePage_->setNearbyRefreshAvailable(true);
    connect(chargePage_, &ChargePage::chargeSafeReadsInvalidated,
            nearbyPage_, &NearbyPage::cancelChargeRefresh);
    connect(chargePage_, &ChargePage::currentAuthorityObserved, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::CurrentOrderResult &result,
                   bool pageOwned, qint64 resolvedOwnerOrderId) {
        const bool reconnectOwned = reconnectCurrentContext_.has_value()
            && context == *reconnectCurrentContext_;
        const qint64 reconnectOwnerOrderId = reconnectCurrentOwnerOrderId_;
        if (reconnectOwned) {
            reconnectCurrentContext_.reset();
            reconnectCurrentOwnerOrderId_ = 0;
        }
        if (result.order.has_value()) {
            adoptActiveOrder(*result.order);
            if (reconnectOwned) {
                resolveReconnectCurrent();
            }
            return;
        }
        const bool guardOwned = guardContext_.has_value() && context == *guardContext_;
        if (guardOwned) {
            clearActiveOrder(0, true);
        } else if (reconnectOwned) {
            clearActiveOrder(reconnectOwnerOrderId);
        } else if (pageOwned) {
            clearActiveOrder(resolvedOwnerOrderId);
        }
        if (reconnectOwned) {
            resolveReconnectCurrent();
        }
    });
    connect(userApi_, &UserApi::chargeRequestFailed, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::ApiError &failure, bool) {
        if (reconnectCurrentContext_.has_value()
            && context == *reconnectCurrentContext_) {
            reconnectCurrentContext_.reset();
            reconnectCurrentOwnerOrderId_ = 0;
            updateReconnectAuthorityUi(reconnectErrorText(failure));
            updateAuthenticatedNavigation();
        }
    });
    connect(chargePage_, &ChargePage::mutationDispatched, this,
            [this](const ev::user::RequestContext &context, qint64 subjectOrderId) {
        mutationAuthorityStamps_.insert(
            context.requestId, {authorityRevision_, subjectOrderId});
    });
    connect(chargePage_, &ChargePage::mutationAuthorityObserved, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::Order &order) {
        const auto stamp = mutationAuthorityStamps_.constFind(context.requestId);
        if (stamp == mutationAuthorityStamps_.cend()) {
            return;
        }
        const bool newerDifferentAuthority = stamp->revisionAtDispatch != authorityRevision_
            && authoritativeActiveOrder_.has_value()
            && authoritativeActiveOrder_->orderId != order.orderId;
        if (newerDifferentAuthority) {
            return;
        }
        const bool active = order.status == QStringLiteral("reserved")
            || order.status == QStringLiteral("charging");
        if (active) {
            adoptActiveOrder(order);
        } else {
            clearActiveOrder(order.orderId);
        }
    });
    connect(chargePage_, &ChargePage::mutationFinished, this,
            [this](const ev::user::RequestContext &context) {
        mutationAuthorityStamps_.remove(context.requestId);
    });
    connect(chargePage_, &ChargePage::chargeFlowBlockedChanged, this, [this](bool blocked) {
        chargeFlowBlocked_ = blocked;
        if (!blocked && deferredSelectionInvalidation_.has_value()) {
            const quint64 selectionGeneration = *deferredSelectionInvalidation_;
            deferredSelectionInvalidation_.reset();
            applySelectionInvalidation(selectionGeneration);
        }
        updateAuthenticatedNavigation();
    });
    connect(chargePage_, &ChargePage::backRequested, this, [this](qint64) {
        const bool authoritativeActive = authoritativeActiveOrder_.has_value()
            && (authoritativeActiveOrder_->status == QStringLiteral("reserved")
                || authoritativeActiveOrder_->status == QStringLiteral("charging"));
        if (authoritativeActive) {
            std::optional<ev::user::StationSelection> matching;
            if (rememberedSelection_.has_value()
                && rememberedSelection_->station.stationId
                    == authoritativeActiveOrder_->stationId
                && rememberedSelection_->charger.chargerId
                    == authoritativeActiveOrder_->chargerId) {
                matching = rememberedSelection_;
            }
            chargePage_->enterOrder(*authoritativeActiveOrder_, matching);
            chargePage_->resume();
            showPage(chargePage_);
            return;
        }
        chargePage_->leavePage();
        hasActiveOrder_ = false;
        authoritativeActiveOrder_.reset();
        rememberedSelection_.reset();
        nearbyNavigationButton_->setEnabled(true);
        currentOrderNavigationButton_->setVisible(false);
        updateAuthenticatedNavigation();
        showPage(nearbyPage_);
    });
    connect(nearbyPage_, &NearbyPage::navigationRequested, this,
            [this](ev::user::GeoPoint origin, ev::user::Station station) {
        if (chargeFlowBlocked_ || reconnectCurrentRequired_) {
            return;
        }
        if (navigationPage_ == nullptr) {
            navigationPage_ = new NavigationPage(mapKey_, pages_);
            pages_->addWidget(navigationPage_);
            connect(navigationPage_, &NavigationPage::backRequested, this, [this] {
                showPage(nearbyPage_);
            });
        }
        navigationPage_->showRoute(origin, station);
        showPage(navigationPage_);
    });

    connect(currentAuthorityRetryButton_, &QPushButton::clicked, this, [this] {
        if (!connected_) {
            userApi_->retryConnection();
            return;
        }
        requestReconnectCurrent();
    });

    connect(userApi_, &UserApi::connectionChanged, this, [this](bool connected) {
        connected_ = connected;
        if (!userApi_->sessionUser().has_value()
            || guardContext_.has_value()
            || pages_->currentWidget() == chargePage_
            || chargeFlowBlocked_) {
            updateReconnectAuthorityUi();
            return;
        }
        reconnectCurrentRequired_ = true;
        reconnectReadsQueued_ = true;
        updateAuthenticatedNavigation();
        if (!connected_) {
            updateReconnectAuthorityUi(
                QStringLiteral("服务器连接不可用，当前订单状态未确认"));
            return;
        }
        requestReconnectCurrent();
    });

    if (!config.serverHost.isEmpty() && config.serverPort != 0) {
        client_->configure(config.serverHost, config.serverPort);
        client_->connectToServer();
    }

    setCentralWidget(centralWidget);
}

void MainWindow::showPage(QWidget *page, NavigationTransition transition)
{
    if (navigationPage_ != nullptr) {
        if (transition == NavigationTransition::SessionReset) {
            navigationPage_->resetForSession();
        } else if (pages_->currentWidget() == navigationPage_
                   && page != navigationPage_) {
            navigationPage_->deactivate();
        }
    }
    pages_->setCurrentWidget(page);
    setSelected(nearbyNavigationButton_, page == nearbyPage_);
    setSelected(currentOrderNavigationButton_, page == chargePage_);
    setSelected(historyNavigationButton_, page == historyPage_);
    setSelected(profileNavigationButton_, page == profilePage_);
}

void MainWindow::updateAuthenticatedNavigation()
{
    const bool authorityGate = chargeFlowBlocked_ || reconnectCurrentRequired_;
    authenticatedNavigation_->setEnabled(!authorityGate);
    nearbyNavigationButton_->setEnabled(!authorityGate && !hasActiveOrder_);
    currentOrderNavigationButton_->setVisible(hasActiveOrder_);
    currentOrderNavigationButton_->setEnabled(!authorityGate && hasActiveOrder_);
    historyNavigationButton_->setEnabled(!authorityGate);
    profileNavigationButton_->setEnabled(!authorityGate);
    pages_->setEnabled(!reconnectCurrentRequired_);
}

void MainWindow::applySelectionInvalidation(quint64 selectionGeneration)
{
    rememberedSelection_.reset();
    chargePage_->invalidateSelection(selectionGeneration);
}

void MainWindow::adoptActiveOrder(const ev::user::Order &order)
{
    if (rememberedSelection_.has_value()
        && (rememberedSelection_->station.stationId != order.stationId
            || rememberedSelection_->charger.chargerId != order.chargerId)) {
        rememberedSelection_.reset();
    }
    authoritativeActiveOrder_ = order;
    hasActiveOrder_ = true;
    ++authorityRevision_;
    updateAuthenticatedNavigation();
}

void MainWindow::clearActiveOrder(qint64 resolvedOwnerOrderId, bool force)
{
    if (!force && authoritativeActiveOrder_.has_value()
        && (resolvedOwnerOrderId <= 0
            || authoritativeActiveOrder_->orderId != resolvedOwnerOrderId)) {
        return;
    }
    const bool changed = authoritativeActiveOrder_.has_value() || hasActiveOrder_;
    authoritativeActiveOrder_.reset();
    hasActiveOrder_ = false;
    if (changed || force) {
        ++authorityRevision_;
    }
    updateAuthenticatedNavigation();
}

void MainWindow::cancelReconnectCurrent()
{
    if (reconnectCurrentContext_.has_value()
        && !reconnectCurrentContext_->requestId.isEmpty()) {
        userApi_->cancelSafeRead(reconnectCurrentContext_->requestId);
    }
    reconnectCurrentContext_.reset();
    reconnectCurrentOwnerOrderId_ = 0;
    updateReconnectAuthorityUi();
    updateAuthenticatedNavigation();
}

void MainWindow::requestReconnectCurrent()
{
    if (!reconnectCurrentRequired_ || !connected_
        || reconnectCurrentContext_.has_value()
        || guardContext_.has_value()
        || pages_->currentWidget() == chargePage_
        || !userApi_->sessionUser().has_value()) {
        updateReconnectAuthorityUi();
        return;
    }
    reconnectCurrentOwnerOrderId_ = authoritativeActiveOrder_.has_value()
        ? authoritativeActiveOrder_->orderId : 0;
    reconnectCurrentContext_ = userApi_->loadCurrentOrder(
        0, 0, ev::user::ChargeOperation::Reconcile);
    if (reconnectCurrentContext_->requestId.isEmpty()) {
        reconnectCurrentContext_.reset();
        reconnectCurrentOwnerOrderId_ = 0;
        updateReconnectAuthorityUi(
            QStringLiteral("当前订单状态校验未发出，请重试"));
        return;
    }
    updateReconnectAuthorityUi();
}

void MainWindow::resolveReconnectCurrent()
{
    reconnectCurrentRequired_ = false;
    updateReconnectAuthorityUi();
    updateAuthenticatedNavigation();
    runQueuedReconnectReads();
}

void MainWindow::runQueuedReconnectReads()
{
    if (!reconnectReadsQueued_ || reconnectCurrentRequired_ || !connected_) {
        return;
    }
    reconnectReadsQueued_ = false;
    nearbyPage_->refreshAfterReconnect();
    historyPage_->refreshAfterReconnect();
}

void MainWindow::updateReconnectAuthorityUi(const QString &message)
{
    const bool visible = reconnectCurrentRequired_
        && userApi_->sessionUser().has_value();
    currentAuthorityStatus_->setVisible(visible);
    currentAuthorityRetryButton_->setVisible(visible);
    if (!visible) {
        currentAuthorityStatus_->clear();
        return;
    }
    if (!message.isEmpty()) {
        currentAuthorityStatus_->setText(message);
    } else if (!connected_) {
        currentAuthorityStatus_->setText(
            QStringLiteral("服务器连接不可用，当前订单状态未确认"));
    } else if (reconnectCurrentContext_.has_value()) {
        currentAuthorityStatus_->setText(QStringLiteral("正在校验当前订单状态…"));
    } else {
        currentAuthorityStatus_->setText(
            QStringLiteral("当前订单状态未确认，请重试校验"));
    }
    currentAuthorityRetryButton_->setText(
        !connected_ ? QStringLiteral("重试连接")
                    : (reconnectCurrentContext_.has_value()
                           ? QStringLiteral("正在校验…")
                           : QStringLiteral("重试订单校验")));
    currentAuthorityRetryButton_->setEnabled(
        !connected_ || !reconnectCurrentContext_.has_value());
}

QString MainWindow::reconnectErrorText(const ev::user::ApiError &error)
{
    if (error.code == QStringLiteral("DB_BUSY")
        || error.code == QStringLiteral("SERVER_BUSY")) {
        return QStringLiteral("服务繁忙，当前订单状态未确认，请重试校验");
    }
    if (error.code == QStringLiteral("TIMEOUT")) {
        return QStringLiteral("当前订单校验超时，请重试");
    }
    if (error.code == QStringLiteral("NOT_CONNECTED")
        || error.code == QStringLiteral("TRANSPORT_ERROR")) {
        return QStringLiteral("服务器连接不可用，当前订单状态未确认");
    }
    if (error.code == QStringLiteral("PROTOCOL_ERROR")) {
        return QStringLiteral("服务器通信异常，当前订单状态未确认，请重试");
    }
    if (error.code == QStringLiteral("INVALID_RESPONSE")) {
        return QStringLiteral("当前订单响应无效，请重试校验");
    }
    return QStringLiteral("当前订单状态未确认，请重试校验");
}
