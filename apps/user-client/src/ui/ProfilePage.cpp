#include "ui/ProfilePage.h"

#include "domain/Formatters.h"
#include "services/UserApi.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
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
    avatar_->setFixedSize(112, 112);
    nicknameEdit_->setObjectName(QStringLiteral("nicknameEdit"));
    nicknameSaveButton_->setObjectName(QStringLiteral("nicknameSaveButton"));
    mobile_->setObjectName(QStringLiteral("profileMobile"));
    mobile_->setReadOnly(true);
    balance_->setObjectName(QStringLiteral("profileBalance"));
    rechargeEdit_->setObjectName(QStringLiteral("rechargeEdit"));
    rechargeEdit_->setPlaceholderText(QStringLiteral("例如 12.34"));
    rechargeButton_->setObjectName(QStringLiteral("rechargeButton"));
    status_->setObjectName(QStringLiteral("profileStatus"));
    status_->setWordWrap(true);
    error_->setObjectName(QStringLiteral("profileError"));
    error_->setWordWrap(true);
    retryButton_->setObjectName(QStringLiteral("profileRetryButton"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(avatar_, 0, Qt::AlignHCenter);
    auto *form = new QFormLayout;
    auto *nicknameRow = new QHBoxLayout;
    nicknameRow->addWidget(nicknameEdit_);
    nicknameRow->addWidget(nicknameSaveButton_);
    form->addRow(QStringLiteral("昵称"), nicknameRow);
    form->addRow(QStringLiteral("手机号"), mobile_);
    form->addRow(QStringLiteral("余额"), balance_);
    auto *rechargeRow = new QHBoxLayout;
    rechargeRow->addWidget(rechargeEdit_);
    rechargeRow->addWidget(rechargeButton_);
    form->addRow(QStringLiteral("充值金额（元）"), rechargeRow);
    layout->addLayout(form);
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
        (void)api_->updateNickname(nicknameEdit_->text());
    });
    connect(rechargeButton_, &QPushButton::clicked, this, [this] {
        (void)api_->rechargeWallet(rechargeEdit_->text());
    });
    connect(retryButton_, &QPushButton::clicked, this, [this] {
        refresh();
    });
    connect(api_, &UserApi::profileUserChanged, this, [this](const ev::user::User &user) {
        displayUser(user);
        hasUser_ = true;
        if (!reconciliationRequired_) {
            error_->clear();
            status_->setText(QStringLiteral("账户信息已更新"));
            retryButton_->setVisible(false);
        }
        updateControls();
    });
    connect(api_, &UserApi::profileReadPendingChanged, this, [this](bool pending) {
        readPending_ = pending;
        if (pending) {
            status_->setText(reconciliationRequired_
                                 ? QStringLiteral("正在重新连接并对账账户信息…")
                                 : QStringLiteral("正在加载账户信息…"));
        }
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
    updateControls();
}

void ProfilePage::setConnectionAvailable(bool available)
{
    connected_ = available;
    if (!available) {
        status_->setText(QStringLiteral("服务器连接不可用"));
        retryButton_->setVisible(true);
    } else if (reconciliationRequired_) {
        status_->setText(QStringLiteral("正在重新连接并对账账户信息…"));
    }
    updateControls();
}

void ProfilePage::displayUser(const ev::user::User &user)
{
    QPixmap pixmap;
    if (!user.avatarPath.isEmpty()) {
        pixmap.load(user.avatarPath);
    }
    if (pixmap.isNull()) {
        pixmap.load(QStringLiteral(":/images/default-avatar.svg"));
    }
    avatar_->setPixmap(pixmap.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    nicknameEdit_->setText(user.nickname);
    mobile_->setText(user.mobile);
    balance_->setText(QStringLiteral("%1 元").arg(formatFen(user.balanceFen)));
}

void ProfilePage::updateControls()
{
    const bool mutationsEnabled = connected_ && hasUser_ && !readPending_ && !mutationPending_
        && !reconciliationRequired_;
    nicknameEdit_->setEnabled(mutationsEnabled);
    nicknameSaveButton_->setEnabled(mutationsEnabled);
    rechargeEdit_->setEnabled(mutationsEnabled);
    rechargeButton_->setEnabled(mutationsEnabled);
    retryButton_->setEnabled(connected_ && !readPending_ && !mutationPending_);
}

void ProfilePage::showProfileFailure(const ev::user::ApiError &failure)
{
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
