#include "ui/HistoryPage.h"

#include "domain/ContractTimestamp.h"
#include "domain/Formatters.h"
#include "services/UserApi.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

QString timestampText(const QString &timestamp)
{
    return timestamp.isEmpty() ? QStringLiteral("—") : timestamp;
}

QLabel *cardLabel(const QString &text, const char *name, const char *role, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(name));
    label->setProperty("role", QString::fromLatin1(role));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

// Keep QListWidget's selection/accessibility model and the existing read-only
// pagination contract; only replace its visual rows with width-aware cards.
class OrderCardList final : public QListWidget
{
public:
    using QListWidget::QListWidget;

    void fitCards()
    {
        const int width = std::max(1, viewport()->width() - 2 * spacing());
        for (int row = 0; row < count(); ++row) {
            auto *entry = item(row);
            auto *card = itemWidget(entry);
            if (!card || !card->layout()) continue;
            const int height = card->layout()->totalHeightForWidth(width);
            const QSize size(width, std::max(height, card->minimumSizeHint().height()));
            if (entry->sizeHint() != size) entry->setSizeHint(size);
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListWidget::resizeEvent(event);
        QTimer::singleShot(0, this, [this] { fitCards(); });
    }
};

} // namespace

HistoryPage::HistoryPage(UserApi *userApi, QWidget *parent)
    : QWidget(parent)
    , userApi_(userApi)
    , list_(new OrderCardList(this))
    , status_(new QLabel(QStringLiteral("尚未加载历史订单"), this))
    , error_(new QLabel(this))
    , prevButton_(new QPushButton(QStringLiteral("上一页"), this))
    , nextButton_(new QPushButton(QStringLiteral("下一页"), this))
    , retryButton_(new QPushButton(QStringLiteral("重试"), this))
    , connectionBanner_(new QLabel(QStringLiteral("服务器连接中…"), this))
    , emptyState_(new QFrame(this))
    , emptyTitle_(new QLabel(this))
    , emptyDescription_(new QLabel(this))
{
    Q_ASSERT(userApi_ != nullptr);
    setObjectName(QStringLiteral("historyPage"));
    list_->setObjectName(QStringLiteral("historyList"));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setSpacing(6);
    list_->setMinimumHeight(240);
    status_->setObjectName(QStringLiteral("historyStatus"));
    status_->setWordWrap(true);
    status_->setProperty("role", QStringLiteral("secondary"));
    error_->setObjectName(QStringLiteral("historyError"));
    error_->setWordWrap(true);
    error_->setProperty("role", QStringLiteral("danger"));
    prevButton_->setObjectName(QStringLiteral("historyPrevButton"));
    nextButton_->setObjectName(QStringLiteral("historyNextButton"));
    retryButton_->setObjectName(QStringLiteral("historyRetryButton"));
    connectionBanner_->setObjectName(QStringLiteral("historyConnectionBanner"));
    connectionBanner_->setWordWrap(true);
    connectionBanner_->setProperty("role", QStringLiteral("danger"));
    connectionBanner_->setStyleSheet(QStringLiteral("color: #BE4B42;"));
    retryButton_->setProperty("role", QStringLiteral("outline"));

    emptyState_->setProperty("role", QStringLiteral("card"));
    auto *emptyLayout = new QVBoxLayout(emptyState_);
    emptyLayout->setContentsMargins(24, 32, 24, 32);
    emptyLayout->setSpacing(12);
    emptyLayout->addStretch();
    auto *emptyIcon = new QLabel(emptyState_);
    emptyIcon->setPixmap(QIcon(QStringLiteral(":/ui/history.svg")).pixmap(QSize(48, 48), devicePixelRatioF()));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);
    emptyTitle_->setObjectName(QStringLiteral("historyEmptyTitle"));
    emptyTitle_->setProperty("role", QStringLiteral("sectionTitle"));
    emptyTitle_->setAlignment(Qt::AlignCenter);
    emptyTitle_->setWordWrap(true);
    emptyDescription_->setProperty("role", QStringLiteral("secondary"));
    emptyDescription_->setAlignment(Qt::AlignCenter);
    emptyDescription_->setWordWrap(true);
    emptyLayout->addWidget(emptyTitle_);
    emptyLayout->addWidget(emptyDescription_);
    emptyLayout->addStretch();

    auto *pager = new QHBoxLayout;
    pager->addWidget(prevButton_);
    pager->addWidget(nextButton_);
    pager->addStretch();
    pager->addWidget(retryButton_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("历史订单"), this);
    title->setProperty("role", QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(connectionBanner_);
    layout->addWidget(status_);
    layout->addWidget(error_);
    layout->addWidget(list_, 1);
    layout->addWidget(emptyState_, 1);
    layout->addLayout(pager);

    connect(prevButton_, &QPushButton::clicked, this, [this] {
        if (committedPageIndex_ > 0) {
            requestPage(committedPageIndex_ - 1);
        }
    });
    connect(nextButton_, &QPushButton::clicked, this, [this] {
        if (committedPage_.has_value()
            && committedPageIndex_ * kPageSize + committedPage_->items.size()
                < committedPage_->total) {
            requestPage(committedPageIndex_ + 1);
        }
    });
    connect(retryButton_, &QPushButton::clicked, this, [this] {
        if (!connected_) {
            reconnectRefreshPending_ = active_;
            userApi_->retryConnection();
            return;
        }
        refresh();
    });
    connect(userApi_, &UserApi::orderHistoryLoaded, this,
            [this](const ev::user::HistoryRequestContext &context,
                   ev::user::OrderListResult result) {
        if (!active_ || !pendingContext_.has_value()
            || context != *pendingContext_
            || context.sessionGeneration != sessionGeneration_
            || context.limit != kPageSize) {
            return;
        }
        const qint64 requestedPageIndex = context.offset / kPageSize;
        if (context.offset != requestedPageIndex * kPageSize) {
            return;
        }
        pendingContext_.reset();
        committedPageIndex_ = requestedPageIndex;
        committedPage_ = std::move(result);
        error_->clear();
        renderCommittedPage();
        updateControls();
    });
    connect(userApi_, &UserApi::orderHistoryRequestFailed, this,
            [this](const ev::user::HistoryRequestContext &context,
                   const ev::user::ApiError &failure) {
        if (!active_ || !pendingContext_.has_value()
            || context != *pendingContext_
            || context.sessionGeneration != sessionGeneration_) {
            return;
        }
        pendingContext_.reset();
        showFailure(failure);
        renderCommittedPage();
        updateControls();
    });
    connect(userApi_, &UserApi::sessionUserApplied, this,
            [this](const ev::user::User &, quint64 generation, quint64) {
        if (sessionGeneration_ != 0 && sessionGeneration_ != generation) {
            clearForSession(generation);
        } else {
            sessionGeneration_ = generation;
        }
    });
    connect(userApi_, &UserApi::connectionChanged,
            this, &HistoryPage::setConnectionAvailable);
    updateControls();
}

void HistoryPage::activate()
{
    active_ = true;
    if (connected_) {
        requestPage(committedPage_.has_value() ? committedPageIndex_ : 0);
    } else {
        reconnectRefreshPending_ = true;
        renderCommittedPage();
        updateControls();
    }
}

void HistoryPage::deactivate()
{
    active_ = false;
    reconnectRefreshPending_ = false;
    cancelPendingRead();
    ++pageGeneration_;
}

void HistoryPage::refresh()
{
    if (active_ && connected_) {
        requestPage(committedPage_.has_value() ? committedPageIndex_ : 0);
    }
}

void HistoryPage::refreshAfterReconnect()
{
    if (active_ && connected_) {
        reconnectRefreshPending_ = false;
        requestPage(committedPage_.has_value() ? committedPageIndex_ : 0);
    } else if (active_) {
        reconnectRefreshPending_ = true;
    }
}

void HistoryPage::setConnectionAvailable(bool available)
{
    connected_ = available;
    if (!connected_) {
        connectionBanner_->setText(QStringLiteral("离线：正在显示最近一次成功缓存"));
        if (active_) {
            reconnectRefreshPending_ = true;
        }
        cancelPendingRead();
        ++pageGeneration_;
        ++readEpoch_;
        renderCommittedPage();
        updateControls();
        return;
    }
    connectionBanner_->setText(QStringLiteral("服务器已连接"));
    updateControls();
}

void HistoryPage::resetForSessionExpiry(quint64 sessionGeneration)
{
    active_ = false;
    reconnectRefreshPending_ = false;
    clearForSession(sessionGeneration);
    status_->setText(QStringLiteral("请重新登录后查看历史订单"));
    updateControls();
}

void HistoryPage::requestPage(qint64 pageIndex)
{
    if (!active_ || !connected_ || pageIndex < 0) {
        return;
    }
    cancelPendingRead();
    ++pageGeneration_;
    ++readEpoch_;
    error_->clear();
    status_->setText(QStringLiteral("正在加载第 %1 页…").arg(pageIndex + 1));
    const auto context = userApi_->loadOrderHistory(
        kPageSize, pageIndex * kPageSize, pageGeneration_, readEpoch_);
    if (context.requestId.isEmpty()) {
        showFailure({{}, QStringLiteral("INVALID_REQUEST"), QStringLiteral("请求失败")});
        updateControls();
        return;
    }
    sessionGeneration_ = context.sessionGeneration;
    pendingContext_ = context;
    updateControls();
}

void HistoryPage::cancelPendingRead()
{
    if (pendingContext_.has_value() && !pendingContext_->requestId.isEmpty()) {
        userApi_->cancelSafeRead(pendingContext_->requestId);
    }
    pendingContext_.reset();
}

void HistoryPage::clearForSession(quint64 sessionGeneration)
{
    cancelPendingRead();
    sessionGeneration_ = sessionGeneration;
    ++pageGeneration_;
    ++readEpoch_;
    committedPageIndex_ = 0;
    committedPage_.reset();
    list_->clear();
    error_->clear();
    status_->setText(QStringLiteral("尚未加载历史订单"));
    updateControls();
}

void HistoryPage::renderCommittedPage()
{
    if (!committedPage_.has_value()) {
        list_->clear();
        status_->setText(connected_ ? QStringLiteral("暂无历史订单")
                                   : QStringLiteral("离线，暂无可用缓存"));
        return;
    }
    QVector<ev::user::Order> display = committedPage_->items;
    std::sort(display.begin(), display.end(), [](const auto &left, const auto &right) {
        const bool leftEnded = !left.endedAt.isEmpty();
        const bool rightEnded = !right.endedAt.isEmpty();
        if (leftEnded != rightEnded) {
            return leftEnded;
        }
        if (leftEnded) {
            const auto endedOrder = ev::user::compareContractTimestamps(
                left.endedAt, right.endedAt);
            if (endedOrder.has_value()
                && *endedOrder != ev::user::TimestampComparison::Equal) {
                return *endedOrder == ev::user::TimestampComparison::Later;
            }
        }
        const auto reservedOrder = ev::user::compareContractTimestamps(
            left.reservedAt, right.reservedAt);
        if (reservedOrder.has_value()
            && *reservedOrder != ev::user::TimestampComparison::Equal) {
            return *reservedOrder == ev::user::TimestampComparison::Later;
        }
        return left.orderId > right.orderId;
    });
    list_->clear();
    for (const auto &order : display) {
        auto *entry = new QListWidgetItem(orderText(order), list_);
        entry->setForeground(Qt::transparent);
        entry->setData(Qt::AccessibleTextRole, orderText(order));
        auto *card = new QFrame;
        card->setProperty("role", QStringLiteral("card"));
        auto *body = new QVBoxLayout(card);
        body->setContentsMargins(16, 14, 16, 14);
        body->setSpacing(6);
        auto *heading = new QHBoxLayout;
        heading->addWidget(cardLabel(QStringLiteral("订单 #%1").arg(order.orderId),
                                    "historyOrderId", "secondary", card), 1);
        auto *badge = new QLabel(statusText(order), card);
        badge->setProperty("role", order.status == QStringLiteral("cancelled")
                           ? QStringLiteral("secondary") : QStringLiteral("available"));
        heading->addWidget(badge);
        body->addLayout(heading);
        body->addWidget(cardLabel(order.stationName, "historyStationName", "sectionTitle", card));
        body->addWidget(cardLabel(QStringLiteral("充电桩 %1").arg(order.chargerCode),
                                  "historyChargerCode", "secondary", card));
        auto *metrics = new QHBoxLayout;
        metrics->addWidget(cardLabel(QStringLiteral("¥%1").arg(formatFen(order.amountFen)),
                                    "historyOrderAmount", "price", card), 1);
        auto *energy = cardLabel(QStringLiteral("%1 kWh").arg(order.energyKwh, 0, 'f', 3),
                                 "historyOrderEnergy", "secondary", card);
        energy->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        metrics->addWidget(energy, 1);
        body->addLayout(metrics);
        const auto timeLine = [card, body](const QString &caption, const QString &value) {
            // Contracts use UTC+08:00. Keep the exact server value in the tooltip
            // and accessible list text; show whole seconds for a readable receipt.
            const bool valid = ev::user::compareContractTimestamps(value, value).has_value();
            QString readable = valid ? value.left(19).replace(QLatin1Char('T'), QLatin1Char(' '))
                                     : timestampText(value);
            auto *label = cardLabel(QStringLiteral("%1：%2").arg(caption, readable),
                                     "historyOrderTime", "secondary", card);
            label->setToolTip(value);
            body->addWidget(label);
        };
        timeLine(QStringLiteral("预约"), order.reservedAt);
        timeLine(QStringLiteral("开始"), order.startedAt);
        timeLine(QStringLiteral("结束"), order.endedAt);
        list_->setItemWidget(entry, card);
    }
    static_cast<OrderCardList *>(list_)->fitCards();
    const QString cacheSuffix = connected_ ? QString() : QStringLiteral(" · 离线缓存");
    status_->setText(QStringLiteral("第 %1 页 · 本页 %2 条 / 共 %3 条%4")
                         .arg(committedPageIndex_ + 1)
                         .arg(display.size())
                         .arg(committedPage_->total)
                         .arg(cacheSuffix));
}

void HistoryPage::updateControls()
{
    const bool idle = !pendingContext_.has_value();
    const bool empty = !committedPage_.has_value() || committedPage_->items.isEmpty();
    error_->setVisible(!error_->text().isEmpty());
    connectionBanner_->setVisible(!connected_);
    if (!connected_) {
        connectionBanner_->setText(committedPage_.has_value()
            ? QStringLiteral("离线：正在显示最近一次成功缓存")
            : QStringLiteral("网络未连接，暂无可用缓存"));
    }
    list_->setVisible(!empty);
    emptyState_->setVisible(empty);
    if (!idle) {
        emptyTitle_->setText(QStringLiteral("正在加载订单"));
        emptyDescription_->setText(QStringLiteral("正在同步你的充电记录，请稍候"));
    } else if (!error_->text().isEmpty()) {
        emptyTitle_->setText(QStringLiteral("暂时无法获取订单"));
        emptyDescription_->setText(QStringLiteral("请查看上方提示，稍后重试"));
    } else if (!connected_ && !committedPage_.has_value()) {
        emptyTitle_->setText(QStringLiteral("暂时离线"));
        emptyDescription_->setText(QStringLiteral("连接恢复后，即可查看充电记录"));
    } else if (committedPageIndex_ > 0) {
        emptyTitle_->setText(QStringLiteral("本页暂无订单"));
        emptyDescription_->setText(QStringLiteral("记录可能已更新，可以返回上一页查看"));
    } else {
        emptyTitle_->setText(QStringLiteral("还没有充电记录"));
        emptyDescription_->setText(QStringLiteral("完成一次充电后，可在这里查看订单、用电量和费用"));
    }
    prevButton_->setVisible(!empty || committedPageIndex_ > 0);
    nextButton_->setVisible(!empty);
    retryButton_->setText(!connected_ ? QStringLiteral("重新连接")
        : (!error_->text().isEmpty() ? QStringLiteral("重试") : QStringLiteral("刷新记录")));
    prevButton_->setEnabled(connected_ && active_ && idle && committedPage_.has_value()
                            && committedPageIndex_ > 0);
    nextButton_->setEnabled(connected_ && active_ && idle && committedPage_.has_value()
                            && committedPageIndex_ * kPageSize
                                   + committedPage_->items.size() < committedPage_->total);
    retryButton_->setEnabled(active_ && idle);
}

void HistoryPage::showFailure(const ev::user::ApiError &failure)
{
    if (failure.code == QStringLiteral("NOT_CONNECTED")
        || failure.code == QStringLiteral("TRANSPORT_ERROR")) {
        error_->setText(QStringLiteral("服务器连接不可用，已保留历史缓存"));
    } else if (failure.code == QStringLiteral("TIMEOUT")) {
        error_->setText(QStringLiteral("历史订单加载超时，已保留原页面"));
    } else if (failure.code == QStringLiteral("PROTOCOL_ERROR")
               || failure.code == QStringLiteral("INVALID_RESPONSE")) {
        error_->setText(QStringLiteral("服务器通信异常，已保留原页面"));
    } else if (failure.code == QStringLiteral("DB_BUSY")
               || failure.code == QStringLiteral("SERVER_BUSY")) {
        error_->setText(QStringLiteral("服务繁忙，请稍后重试，已保留原页面"));
    } else if (failure.code == QStringLiteral("AUTH_REQUIRED")
               || failure.code == QStringLiteral("FORBIDDEN")) {
        error_->setText(QStringLiteral("登录状态已失效，请重新登录"));
    } else {
        error_->setText(QStringLiteral("历史订单加载失败，已保留原页面"));
    }
}

QString HistoryPage::orderText(const ev::user::Order &order)
{
    return QStringLiteral("订单 #%1 · %2\n充电站：%3 · 充电桩：%4\n预约：%5 · 开始：%6 · 结束：%7\n%8 kWh · %9 元")
        .arg(order.orderId)
        .arg(statusText(order))
        .arg(order.stationName, order.chargerCode)
        .arg(timestampText(order.reservedAt), timestampText(order.startedAt),
             timestampText(order.endedAt))
        .arg(order.energyKwh, 0, 'f', 3)
        .arg(formatFen(order.amountFen));
}

QString HistoryPage::statusText(const ev::user::Order &order)
{
    if (order.status == QStringLiteral("reserved")) {
        return QStringLiteral("已预约");
    }
    if (order.status == QStringLiteral("charging")) {
        return order.endedAt.isEmpty() ? QStringLiteral("充电中")
                                       : QStringLiteral("已停止待结算");
    }
    if (order.status == QStringLiteral("completed")) {
        return QStringLiteral("已完成");
    }
    if (order.status == QStringLiteral("cancelled")) {
        return QStringLiteral("已取消");
    }
    return QStringLiteral("状态未知");
}
