#pragma once

#include "domain/Models.h"

#include <QWidget>

#include <optional>

class UserApi;

class HistoryPage final : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPage(UserApi *userApi, QWidget *parent = nullptr);

public slots:
    void activate();
    void deactivate();
    void refresh();
    void refreshAfterReconnect();
    void setConnectionAvailable(bool available);

private:
    static constexpr qint64 kPageSize = 20;

    void requestPage(qint64 pageIndex);
    void cancelPendingRead();
    void clearForSession(quint64 sessionGeneration);
    void renderCommittedPage();
    void updateControls();
    void showFailure(const ev::user::ApiError &error);
    [[nodiscard]] static QString orderText(const ev::user::Order &order);
    [[nodiscard]] static QString statusText(const QString &status);

    UserApi *userApi_;
    class QListWidget *list_;
    class QLabel *status_;
    class QLabel *error_;
    class QPushButton *prevButton_;
    class QPushButton *nextButton_;
    class QPushButton *retryButton_;
    class QLabel *connectionBanner_;
    bool active_ = false;
    bool connected_ = false;
    bool reconnectRefreshPending_ = false;
    quint64 sessionGeneration_ = 0;
    quint64 pageGeneration_ = 0;
    quint64 readEpoch_ = 0;
    qint64 committedPageIndex_ = 0;
    std::optional<ev::user::OrderListResult> committedPage_;
    std::optional<ev::user::HistoryRequestContext> pendingContext_;
};
