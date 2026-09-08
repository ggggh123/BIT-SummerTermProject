#pragma once

#include "domain/Models.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class UserApi;
class UsageChart;

class ProfilePage final : public QWidget
{
    Q_OBJECT

public:
    explicit ProfilePage(UserApi *api, QWidget *parent = nullptr);

    void refresh();

signals:
    void accountExitRequested(bool switchAccount);

public slots:
    void setConnectionAvailable(bool available);
    void resetForSessionExpiry();

private:
    void displayUser(const ev::user::User &user);
    void displayStatistics(const ev::user::UsageStatistics &statistics);
    void clearStatistics();
    void refreshStatistics();
    void refreshAccountStatus();
    void updateControls();
    void showProfileFailure(const ev::user::ApiError &error);
    static QString localizedError(const ev::user::ApiError &error);

    UserApi *api_;
    QLabel *avatar_;
    QLabel *displayName_;
    QLineEdit *nicknameEdit_;
    QPushButton *nicknameSaveButton_;
    QLineEdit *mobile_;
    QLabel *balance_;
    QLineEdit *rechargeEdit_;
    QPushButton *rechargeButton_;
    QLabel *status_;
    QLabel *error_;
    QPushButton *retryButton_;
    QPushButton *accountMenuButton_ = nullptr;
    QPushButton *statisticsRefreshButton_ = nullptr;
    QLabel *registeredAt_ = nullptr;
    QLabel *totalEnergy_ = nullptr;
    QLabel *monthEnergy_ = nullptr;
    QLabel *paidAmount_ = nullptr;
    QLabel *chargeCount_ = nullptr;
    QLabel *usageDetails_ = nullptr;
    QLabel *settlementHint_ = nullptr;
    QLabel *statisticsStatus_ = nullptr;
    QLabel *walletFeedback_ = nullptr;
    QLabel *accountState_ = nullptr;
    QWidget *frozenNotice_ = nullptr;
    QPushButton *stateRefreshButton_ = nullptr;
    QTimer *statePollTimer_ = nullptr;
    qint64 displayedUserId_ = 0;
    bool frozen_ = false;
    bool backgroundRefresh_ = false;
    bool backgroundFailure_ = false;
    QString backgroundRequestId_;
    UsageChart *usageChart_ = nullptr;
    QString statisticsRequestId_;
    bool hasStatistics_ = false;
    bool rechargeFeedback_ = false;
    bool connected_ = false;
    bool readPending_ = false;
    bool mutationPending_ = false;
    bool hasUser_ = false;
    bool reconciliationRequired_ = false;
};
