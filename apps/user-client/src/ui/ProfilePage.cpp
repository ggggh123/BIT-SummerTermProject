#include "ui/ProfilePage.h"

#include "domain/Formatters.h"
#include "services/UserApi.h"
#include "ui/UsageChart.h"

#include <QFrame>
#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QString kUncertainMessage = QStringLiteral("结果未确认，请重新连接后刷新账户信息");

bool isKnownInlineFailureCode(const QString &code)
{
    static const QSet<QString> knownCodes{
        QStringLiteral("INVALID_REQUEST"), QStringLiteral("UNSUPPORTED_VERSION"),
        QStringLiteral("AUTH_REQUIRED"), QStringLiteral("FORBIDDEN"),
        QStringLiteral("INVALID_PHONE"), QStringLiteral("INVALID_CREDENTIALS"),
        QStringLiteral("ENTITY_NOT_FOUND"), QStringLiteral("USER_FROZEN"),
        QStringLiteral("ACTIVE_ORDER_EXISTS"), QStringLiteral("CHARGER_NOT_AVAILABLE"),
        QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("INSUFFICIENT_BALANCE"),
        QStringLiteral("MAP_API_ERROR"), QStringLiteral("FORECAST_INVALID"),
        QStringLiteral("FORECAST_STALE"), QStringLiteral("SERVER_BUSY"),
        QStringLiteral("DB_BUSY"), QStringLiteral("INTERNAL_ERROR"),
        QStringLiteral("INVALID_NICKNAME"), QStringLiteral("INVALID_AMOUNT"),
        QStringLiteral("PROFILE_BUSY"), QStringLiteral("RECONCILIATION_REQUIRED"),
    };
    return knownCodes.contains(code);
}

} // namespace

ProfilePage::ProfilePage(UserApi *api, QWidget *parent)
    : QWidget(parent)
    , api_(api)
    , avatar_(new QLabel(this))
    , displayName_(new QLabel(this))
    , nicknameEdit_(new QLineEdit(this))
    , nicknameSaveButton_(new QPushButton(QStringLiteral("保存昵称"), this))
    , mobile_(new QLineEdit(this))
    , balance_(new QLabel(this))
    , rechargeEdit_(new QLineEdit(this))
    , rechargeButton_(new QPushButton(QStringLiteral("模拟充值"), this))
    , status_(new QLabel(this))
    , error_(new QLabel(this))
    , retryButton_(new QPushButton(QStringLiteral("刷新账户信息"), this))
{
    Q_ASSERT(api_ != nullptr);
    setObjectName(QStringLiteral("profilePage"));

    avatar_->setObjectName(QStringLiteral("profileAvatar"));
    avatar_->setAlignment(Qt::AlignCenter);
    avatar_->setFixedSize(56, 56);
    displayName_->setObjectName(QStringLiteral("profileDisplayName"));
    displayName_->setProperty("role", QStringLiteral("sectionTitle"));
    displayName_->setTextFormat(Qt::PlainText);
    displayName_->setWordWrap(true);
    displayName_->setMinimumWidth(0);
    displayName_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nicknameEdit_->setObjectName(QStringLiteral("nicknameEdit"));
    nicknameSaveButton_->setObjectName(QStringLiteral("nicknameSaveButton"));
    mobile_->setObjectName(QStringLiteral("profileMobile"));
    mobile_->setAccessibleName(QStringLiteral("手机号"));
    mobile_->setReadOnly(true);
    mobile_->setFocusPolicy(Qt::NoFocus);
    balance_->setObjectName(QStringLiteral("profileBalance"));
    balance_->setProperty("role", QStringLiteral("chargeMetric"));
    balance_->setWordWrap(true);
    balance_->setMinimumWidth(0);
    balance_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    rechargeEdit_->setObjectName(QStringLiteral("rechargeEdit"));
    rechargeEdit_->setPlaceholderText(QStringLiteral("输入充值金额（元）"));
    rechargeEdit_->setAccessibleName(QStringLiteral("充值金额（元）"));
    rechargeEdit_->setMinimumWidth(0);
    nicknameEdit_->setMinimumWidth(0);
    nicknameEdit_->setAccessibleName(QStringLiteral("昵称"));
    rechargeButton_->setObjectName(QStringLiteral("rechargeButton"));
    rechargeButton_->setProperty("role", QStringLiteral("primary"));
    nicknameSaveButton_->setProperty("role", QStringLiteral("outline"));
    status_->setObjectName(QStringLiteral("profileStatus"));
    status_->setWordWrap(true);
    status_->setProperty("role", QStringLiteral("secondary"));
    error_->setObjectName(QStringLiteral("profileError"));
    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    error_->setProperty("role", QStringLiteral("danger"));
    retryButton_->setObjectName(QStringLiteral("profileRetryButton"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    auto *title = new QLabel(QStringLiteral("我的账户"), this);
    title->setProperty("role", QStringLiteral("pageTitle"));
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(title, 1);
    accountMenuButton_ = new QPushButton(QStringLiteral("账号管理"), this);
    accountMenuButton_->setObjectName(QStringLiteral("accountMenuButton"));
    accountMenuButton_->setProperty("role", QStringLiteral("outline"));
    auto *menu = new QMenu(accountMenuButton_);
    auto *switchAccount = menu->addAction(QStringLiteral("切换账号"));
    switchAccount->setObjectName(QStringLiteral("switchAccountAction"));
    auto *logout = menu->addAction(QStringLiteral("退出当前账号"));
    logout->setObjectName(QStringLiteral("logoutAction"));
    accountMenuButton_->setMenu(menu);
    titleRow->addWidget(accountMenuButton_);
    layout->addLayout(titleRow);
    connect(switchAccount, &QAction::triggered, this, [this] { emit accountExitRequested(true); });
    connect(logout, &QAction::triggered, this, [this] { emit accountExitRequested(false); });
    auto *identity = new QHBoxLayout;
    identity->setSpacing(12);
    identity->addWidget(avatar_, 0, Qt::AlignTop);
    auto *identityText = new QVBoxLayout;
    identityText->setSpacing(2);
    identityText->addWidget(displayName_);
    identityText->addWidget(mobile_);
    identity->addLayout(identityText, 1);
    layout->addLayout(identity);

    auto *stateRow = new QHBoxLayout;
    accountState_ = new QLabel(this);
    accountState_->setObjectName(QStringLiteral("profileAccountState"));
    accountState_->setAccessibleName(QStringLiteral("账号状态"));
    stateRow->addWidget(accountState_, 0, Qt::AlignLeft);
    stateRow->addStretch();
    stateRefreshButton_ = new QPushButton(QStringLiteral("刷新状态"), this);
    stateRefreshButton_->setObjectName(QStringLiteral("profileStateRefreshButton"));
    stateRefreshButton_->setProperty("role", QStringLiteral("outline"));
    stateRefreshButton_->setToolTip(QStringLiteral("以服务端为准；账户页空闲时每 5 秒自动同步，编辑输入期间暂停自动同步"));
    stateRow->addWidget(stateRefreshButton_);
    layout->addLayout(stateRow);
    auto *frozenNotice = new QFrame(this);
    frozenNotice_ = frozenNotice;
    frozenNotice_->setObjectName(QStringLiteral("profileFrozenNotice"));
    auto *frozenLayout = new QVBoxLayout(frozenNotice);
    frozenLayout->setContentsMargins(14, 12, 14, 12);
    frozenLayout->setSpacing(6);
    auto *frozenTitle = new QLabel(QStringLiteral("账号已冻结 · 部分操作受限"), frozenNotice);
    frozenTitle->setObjectName(QStringLiteral("profileFrozenTitle"));
    auto *frozenDescription = new QLabel(QStringLiteral(
        "暂不可预约、开始充电或充值。已有订单仍可取消预约、停止充电和结算，用电记录可正常查看。\n请联系管理员解除冻结。"), frozenNotice);
    frozenDescription->setObjectName(QStringLiteral("profileFrozenDescription"));
    for (auto *label : {frozenTitle, frozenDescription}) {
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        frozenLayout->addWidget(label);
    }
    frozenNotice_->hide();
    layout->addWidget(frozenNotice_);

    auto *wallet = new QFrame(this);
    wallet->setProperty("role", QStringLiteral("card"));
    auto *walletLayout = new QVBoxLayout(wallet);
    walletLayout->setContentsMargins(16, 16, 16, 16);
    walletLayout->setSpacing(10);
    auto *balanceTitle = new QLabel(QStringLiteral("账户余额（元）"), wallet);
    balanceTitle->setProperty("role", QStringLiteral("secondary"));
    walletLayout->addWidget(balanceTitle);
    walletLayout->addWidget(balance_);
    auto *rechargeRow = new QHBoxLayout;
    rechargeRow->addWidget(rechargeEdit_, 1);
    rechargeRow->addWidget(rechargeButton_);
    walletLayout->addLayout(rechargeRow);
    auto *rechargeHint = new QLabel(QStringLiteral("项目演示充值，不涉及真实支付"), wallet);
    rechargeHint->setProperty("role", QStringLiteral("secondary"));
    rechargeHint->setWordWrap(true);
    walletLayout->addWidget(rechargeHint);
    walletFeedback_ = new QLabel(wallet);
    walletFeedback_->setObjectName(QStringLiteral("walletFeedback"));
    walletFeedback_->setProperty("role", QStringLiteral("danger"));
    walletFeedback_->setWordWrap(true);
    walletFeedback_->setTextFormat(Qt::PlainText);
    walletFeedback_->hide();
    walletLayout->addWidget(walletFeedback_);
    layout->addWidget(wallet);

    auto *usage = new QFrame(this);
    usage->setObjectName(QStringLiteral("profileUsagePanel"));
    usage->setProperty("role", QStringLiteral("card"));
    auto *usageLayout = new QVBoxLayout(usage);
    usageLayout->setContentsMargins(16, 16, 16, 16);
    usageLayout->setSpacing(12);
    auto *usageHeader = new QHBoxLayout;
    auto *usageTitle = new QLabel(QStringLiteral("我的用电足迹"), usage);
    usageTitle->setProperty("role", QStringLiteral("sectionTitle"));
    usageHeader->addWidget(usageTitle, 1);
    statisticsRefreshButton_ = new QPushButton(QStringLiteral("刷新"), usage);
    statisticsRefreshButton_->setObjectName(QStringLiteral("profileStatisticsRefreshButton"));
    statisticsRefreshButton_->setProperty("role", QStringLiteral("outline"));
    usageHeader->addWidget(statisticsRefreshButton_);
    usageLayout->addLayout(usageHeader);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(16);
    const auto metric = [usage, grid](const QString &caption, const char *name, int row, int col) {
        auto *cell = new QVBoxLayout;
        cell->setSpacing(3);
        auto *label = new QLabel(caption, usage);
        label->setProperty("role", QStringLiteral("secondary"));
        auto *value = new QLabel(QStringLiteral("—"), usage);
        value->setObjectName(QString::fromLatin1(name));
        value->setProperty("role", QStringLiteral("usageMetric"));
        value->setMinimumWidth(0);
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        value->setWordWrap(true);
        cell->addWidget(label);
        cell->addWidget(value);
        grid->addLayout(cell, row, col);
        grid->setColumnStretch(col, 1);
        return value;
    };
    totalEnergy_ = metric(QStringLiteral("累计用电 · kWh"), "profileTotalEnergy", 0, 0);
    monthEnergy_ = metric(QStringLiteral("本月用电 · kWh"), "profileMonthEnergy", 0, 1);
    paidAmount_ = metric(QStringLiteral("已结算费用 · 元"), "profilePaidAmount", 1, 0);
    chargeCount_ = metric(QStringLiteral("充电次数"), "profileChargeCount", 1, 1);
    usageLayout->addLayout(grid);
    usageDetails_ = new QLabel(usage);
    usageDetails_->setObjectName(QStringLiteral("profileUsageDetails"));
    usageDetails_->setProperty("role", QStringLiteral("secondary"));
    usageDetails_->setWordWrap(true);
    usageLayout->addWidget(usageDetails_);
    settlementHint_ = new QLabel(usage);
    settlementHint_->setObjectName(QStringLiteral("profileSettlementHint"));
    settlementHint_->setProperty("role", QStringLiteral("danger"));
    settlementHint_->setWordWrap(true);
    usageLayout->addWidget(settlementHint_);
    auto *chartTitle = new QLabel(QStringLiteral("近 7 天用电 / kWh"), usage);
    chartTitle->setProperty("role", QStringLiteral("sectionTitle"));
    usageLayout->addWidget(chartTitle);
    usageChart_ = new UsageChart(usage);
    usageLayout->addWidget(usageChart_);
    auto *usageNote = new QLabel(QStringLiteral("按订单开始日期（北京时间）归集，含进行中订单的已充电量；费用仅计已结算订单。"), usage);
    usageNote->setProperty("role", QStringLiteral("secondary"));
    usageNote->setWordWrap(true);
    usageLayout->addWidget(usageNote);
    statisticsStatus_ = new QLabel(usage);
    statisticsStatus_->setObjectName(QStringLiteral("profileStatisticsStatus"));
    statisticsStatus_->setProperty("role", QStringLiteral("secondary"));
    statisticsStatus_->setWordWrap(true);
    usageLayout->addWidget(statisticsStatus_);
    layout->addWidget(usage);
    clearStatistics();
    connect(statisticsRefreshButton_, &QPushButton::clicked, this, &ProfilePage::refreshStatistics);
    connect(api_, &UserApi::usageStatisticsLoaded, this,
        [this](const QString &id, const ev::user::UsageStatistics &data) {
            if (id != statisticsRequestId_ || !api_->sessionUser()
                || api_->sessionUser()->userId != data.userId) return;
            statisticsRequestId_.clear();
            displayStatistics(data);
            updateControls();
        });
    connect(api_, &UserApi::usageStatisticsFailed, this, [this](const ev::user::ApiError &failure) {
        if (failure.requestId != statisticsRequestId_) return;
        statisticsRequestId_.clear();
        statisticsStatus_->setText(failure.code == QStringLiteral("INVALID_REQUEST")
            ? QStringLiteral("服务端尚未支持个人统计，请更新服务端后刷新；账户功能仍可使用。")
            : (hasStatistics_ ? QStringLiteral("统计更新失败，保留上次数据，请刷新重试。")
                              : QStringLiteral("统计暂不可用，请刷新重试。")));
        updateControls();
    });

    auto *details = new QFrame(this);
    details->setProperty("role", QStringLiteral("card"));
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(16, 16, 16, 16);
    detailsLayout->setSpacing(10);
    auto *detailsTitle = new QLabel(QStringLiteral("账户资料"), details);
    detailsTitle->setProperty("role", QStringLiteral("sectionTitle"));
    detailsLayout->addWidget(detailsTitle);
    registeredAt_ = new QLabel(details);
    registeredAt_->setObjectName(QStringLiteral("profileRegisteredAt"));
    registeredAt_->setProperty("role", QStringLiteral("secondary"));
    registeredAt_->setWordWrap(true);
    detailsLayout->addWidget(registeredAt_);
    auto *nicknameRow = new QHBoxLayout;
    nicknameRow->setSpacing(8);
    nicknameRow->addWidget(nicknameEdit_, 1);
    nicknameRow->addWidget(nicknameSaveButton_);
    detailsLayout->addLayout(nicknameRow);
    layout->addWidget(details);
    layout->addWidget(status_);
    layout->addWidget(error_);
    layout->addWidget(retryButton_, 0, Qt::AlignLeft);
    layout->addStretch();

    status_->setText(QStringLiteral("账户信息尚未加载"));
    retryButton_->setVisible(false);
    displayUser(api_->sessionUser().value_or(ev::user::User{}));
    hasUser_ = api_->sessionUser().has_value();
    updateControls();

    connect(nicknameSaveButton_, &QPushButton::clicked, this, [this] {
        rechargeFeedback_ = false;
        (void)api_->updateNickname(nicknameEdit_->text());
    });
    connect(rechargeButton_, &QPushButton::clicked, this, [this] {
        rechargeFeedback_ = true;
        (void)api_->rechargeWallet(rechargeEdit_->text());
    });
    connect(retryButton_, &QPushButton::clicked, this, [this] {
        refresh();
    });
    connect(stateRefreshButton_, &QPushButton::clicked, this, &ProfilePage::refresh);
    statePollTimer_ = new QTimer(this);
    statePollTimer_->setObjectName(QStringLiteral("profileStatePollTimer"));
    statePollTimer_->setInterval(5'000);
    connect(statePollTimer_, &QTimer::timeout, this, &ProfilePage::refreshAccountStatus);
    statePollTimer_->start();
    connect(api_, &UserApi::sessionUserApplied, this,
            [this](const ev::user::User &user, quint64, quint64) {
        displayUser(user);
        hasUser_ = true;
        // 定时只读更新不能吞掉用户刚遇到的充值／昵称错误。
        if (!reconciliationRequired_ && (!backgroundRefresh_ || backgroundFailure_)) {
            error_->clear();
            status_->setText(QStringLiteral("账户信息已更新"));
            retryButton_->setVisible(false);
        }
        backgroundFailure_ = false;
        updateControls();
    });
    connect(api_, &UserApi::profileReadPendingChanged, this, [this](bool pending) {
        readPending_ = pending;
        if (pending && !backgroundRefresh_) {
            status_->setText(reconciliationRequired_
                                 ? QStringLiteral("正在重新连接并对账账户信息…")
                                 : QStringLiteral("正在加载账户信息…"));
        }
        if (!pending) backgroundRefresh_ = false;
        updateControls();
    });
    connect(api_, &UserApi::profileMutationPendingChanged, this, [this](bool pending) {
        mutationPending_ = pending;
        if (pending) {
            error_->clear();
            status_->setText(QStringLiteral("正在提交账户变更…"));
        }
        updateControls();
    });
    connect(api_, &UserApi::profileRequestFailed, this, &ProfilePage::showProfileFailure);
    connect(api_, &UserApi::connectionChanged, this, &ProfilePage::setConnectionAvailable);
    connect(api_, &UserApi::profileReconciliationRequired, this, [this] {
        reconciliationRequired_ = true;
        error_->setText(kUncertainMessage);
        status_->setText(kUncertainMessage);
        retryButton_->setVisible(true);
        updateControls();
    });
    connect(api_, &UserApi::profileReconciled, this, [this](const ev::user::User &user) {
        reconciliationRequired_ = false;
        displayUser(user);
        hasUser_ = true;
        error_->clear();
        status_->setText(QStringLiteral("账户信息已对账"));
        retryButton_->setVisible(false);
        updateControls();
    });
}

void ProfilePage::refresh()
{
    backgroundRefresh_ = false;
    if (const auto cached = api_->sessionUser(); cached.has_value()) {
        displayUser(*cached);
        hasUser_ = true;
    }
    reconciliationRequired_ = api_->profileNeedsReconciliation();
    if (reconciliationRequired_) {
        error_->setText(kUncertainMessage);
    } else {
        error_->clear();
    }
    (void)api_->loadProfile();
    refreshStatistics();
    updateControls();
}

void ProfilePage::refreshAccountStatus()
{
    // 只轮询当前可见且空闲的账户页，不干扰输入、写请求或不确定结果的对账。
    if (!isVisible() || !connected_ || !hasUser_ || readPending_ || mutationPending_
        || reconciliationRequired_ || api_->profileNeedsReconciliation()
        || nicknameEdit_->hasFocus() || rechargeEdit_->hasFocus()) return;
    backgroundRefresh_ = true;
    backgroundRequestId_ = api_->loadProfile();
    if (backgroundRequestId_.isEmpty()) backgroundRefresh_ = false;
}

void ProfilePage::setConnectionAvailable(bool available)
{
    connected_ = available;
    if (!available) {
        status_->setText(QStringLiteral("服务器连接不可用"));
        statisticsStatus_->setText(hasStatistics_ ? QStringLiteral("离线 · 显示上次成功统计")
                                                 : QStringLiteral("离线 · 统计尚未加载"));
        retryButton_->setVisible(true);
    } else if (reconciliationRequired_) {
        status_->setText(QStringLiteral("正在重新连接并对账账户信息…"));
    }
    updateControls();
}

void ProfilePage::resetForSessionExpiry()
{
    backgroundRefresh_ = false;
    backgroundFailure_ = false;
    backgroundRequestId_.clear();
    rechargeFeedback_ = false;
    api_->cancelSafeRead(statisticsRequestId_);
    statisticsRequestId_.clear();
    clearStatistics();
    readPending_ = false;
    mutationPending_ = false;
    hasUser_ = false;
    reconciliationRequired_ = false;
    displayUser({});
    rechargeEdit_->clear();
    status_->setText(QStringLiteral("请重新登录后查看账户信息"));
    error_->clear();
    retryButton_->hide();
    updateControls();
}

void ProfilePage::displayUser(const ev::user::User &user)
{
    const bool preserveDraft = user.userId > 0 && displayedUserId_ == user.userId
        && nicknameEdit_->isModified() && !(mutationPending_ && !rechargeFeedback_);
    displayedUserId_ = user.userId;
    frozen_ = user.status == QStringLiteral("frozen");
    accountState_->setText(frozen_ ? QStringLiteral("账号已冻结") : QStringLiteral("账号正常"));
    accountState_->setVisible(user.userId > 0);
    accountState_->setProperty("frozen", frozen_);
    accountState_->style()->unpolish(accountState_);
    accountState_->style()->polish(accountState_);
    accountState_->update();
    frozenNotice_->setVisible(user.userId > 0 && frozen_);
    QPixmap pixmap;
    if (!user.avatarPath.isEmpty()) {
        pixmap.load(user.avatarPath);
    }
    if (pixmap.isNull()) {
        pixmap = QIcon(QStringLiteral(":/ui/person.svg")).pixmap(QSize(56, 56), devicePixelRatioF());
    } else {
        pixmap = pixmap.scaled(QSize(56, 56) * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(devicePixelRatioF());
    }
    avatar_->setPixmap(pixmap);
    displayName_->setText(user.nickname.isEmpty() ? QStringLiteral("充电用户") : user.nickname);
    if (!preserveDraft) nicknameEdit_->setText(user.nickname);
    mobile_->setText(user.mobile);
    balance_->setText(formatFen(user.balanceFen));
    registeredAt_->setText(user.userId > 0
        ? QStringLiteral("账号 #%1 · 注册于 %2").arg(user.userId).arg(user.registeredAt.left(10)) : QString());
}

void ProfilePage::clearStatistics()
{
    hasStatistics_ = false;
    for (auto *label : {totalEnergy_, monthEnergy_, paidAmount_, chargeCount_}) label->setText(QStringLiteral("—"));
    usageDetails_->clear();
    settlementHint_->clear();
    settlementHint_->hide();
    usageChart_->setDays({});
    statisticsStatus_->setText(QStringLiteral("个人用电统计尚未加载"));
}

void ProfilePage::refreshStatistics()
{
    api_->cancelSafeRead(statisticsRequestId_);
    statisticsRequestId_ = api_->loadUsageStatistics();
    statisticsStatus_->setText(statisticsRequestId_.isEmpty() ? QStringLiteral("请登录后查看用电统计")
        : (hasStatistics_ ? QStringLiteral("正在更新 · 暂示上次统计") : QStringLiteral("正在读取个人用电统计…")));
    updateControls();
}

void ProfilePage::displayStatistics(const ev::user::UsageStatistics &data)
{
    hasStatistics_ = true;
    totalEnergy_->setText(QString::number(data.energyKwh, 'f', 3));
    monthEnergy_->setText(QString::number(data.monthEnergyKwh, 'f', 3));
    paidAmount_->setText(formatFen(data.paidFen));
    chargeCount_->setText(QString::number(data.orderCount));
    usageDetails_->setText(QStringLiteral("累计充电 %1 小时 %2 分钟 · 已结算 %3 次")
        .arg(data.durationSec / 3600).arg((data.durationSec / 60) % 60).arg(data.completedCount));
    settlementHint_->setVisible(data.pendingSettlementCount > 0);
    settlementHint_->setText(data.pendingSettlementCount > 0
        ? QStringLiteral("%1 笔待结算 · 应付 ¥%2\n充值后，请返回「当前订单」确认结算。")
            .arg(data.pendingSettlementCount).arg(formatFen(data.pendingSettlementFen)) : QString());
    usageChart_->setDays(data.days);
    statisticsStatus_->setText(QStringLiteral("统计更新于 %1").arg(data.asOf.left(19).replace('T', ' ')));
}

void ProfilePage::updateControls()
{
    // 统计区变长后，充值失败仍应紧邻输入框可见，而不是只出现在页面底部。
    walletFeedback_->setText(rechargeFeedback_ ? error_->text() : QString());
    walletFeedback_->setVisible(!walletFeedback_->text().isEmpty());
    accountMenuButton_->setEnabled(hasUser_ && api_->canLogout() && !mutationPending_);
    accountMenuButton_->setToolTip(accountMenuButton_->isEnabled() ? QStringLiteral("切换账号或退出本机登录")
        : QStringLiteral("请等待账户或订单操作确认后再切换账号"));
    statisticsRefreshButton_->setEnabled(hasUser_ && connected_ && statisticsRequestId_.isEmpty());
    stateRefreshButton_->setEnabled(hasUser_ && connected_ && !readPending_ && !mutationPending_);
    accountState_->setToolTip(connected_ ? QStringLiteral("服务端最近一次确认的账号状态")
                                       : QStringLiteral("离线：保留服务端上次确认状态，请连接后刷新"));
    error_->setVisible(!error_->text().isEmpty());
    const bool mutationsEnabled = connected_ && hasUser_ && !readPending_ && !mutationPending_
        && !reconciliationRequired_;
    nicknameEdit_->setEnabled(mutationsEnabled);
    nicknameSaveButton_->setEnabled(mutationsEnabled);
    rechargeEdit_->setEnabled(mutationsEnabled && !frozen_);
    rechargeButton_->setEnabled(mutationsEnabled && !frozen_);
    rechargeButton_->setToolTip(frozen_ ? QStringLiteral("账号已冻结，请联系管理员解除冻结后充值") : QString());
    retryButton_->setEnabled(connected_ && !readPending_ && !mutationPending_);
}

void ProfilePage::showProfileFailure(const ev::user::ApiError &failure)
{
    backgroundFailure_ = !backgroundRequestId_.isEmpty() && failure.requestId == backgroundRequestId_;
    if (api_->profileNeedsReconciliation()) {
        reconciliationRequired_ = true;
        error_->setText(kUncertainMessage);
        status_->setText(kUncertainMessage);
        retryButton_->setVisible(true);
    } else {
        error_->setText(localizedError(failure));
        status_->setText(QStringLiteral("账户操作失败"));
        retryButton_->setVisible(failure.code == QStringLiteral("NOT_CONNECTED")
                                 || failure.code == QStringLiteral("TRANSPORT_ERROR")
                                 || failure.code == QStringLiteral("PROTOCOL_ERROR")
                                 || failure.code == QStringLiteral("TIMEOUT")
                                 || failure.code == QStringLiteral("INVALID_RESPONSE")
                                 || !isKnownInlineFailureCode(failure.code));
    }
    updateControls();
}

QString ProfilePage::localizedError(const ev::user::ApiError &failure)
{
    if (failure.code == QStringLiteral("NOT_CONNECTED")) {
        return QStringLiteral("服务器连接不可用");
    }
    if (failure.code == QStringLiteral("TRANSPORT_ERROR")) {
        return QStringLiteral("服务器连接中断，请重试");
    }
    if (failure.code == QStringLiteral("TIMEOUT")) {
        return QStringLiteral("服务器响应超时，请重试");
    }
    if (failure.code == QStringLiteral("PROTOCOL_ERROR")) {
        return QStringLiteral("通信协议异常，请重试");
    }
    if (failure.code == QStringLiteral("INVALID_RESPONSE")) {
        return QStringLiteral("服务器返回的账户信息无效");
    }
    if (!isKnownInlineFailureCode(failure.code)) {
        return QStringLiteral("账户操作失败，请重试");
    }
    return failure.message.isEmpty() ? QStringLiteral("账户操作失败，请重试") : failure.message;
}
