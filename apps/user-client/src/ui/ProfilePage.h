#pragma once

#include "domain/Models.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class UserApi;

class ProfilePage final : public QWidget
{
    Q_OBJECT

public:
    explicit ProfilePage(UserApi *api, QWidget *parent = nullptr);

    void refresh();

public slots:
    void setConnectionAvailable(bool available);
    void resetForSessionExpiry();

private:
    void displayUser(const ev::user::User &user);
    void updateControls();
    void showProfileFailure(const ev::user::ApiError &error);
    static QString localizedError(const ev::user::ApiError &error);

    UserApi *api_;
    QLabel *avatar_;
    QLineEdit *nicknameEdit_;
    QPushButton *nicknameSaveButton_;
    QLineEdit *mobile_;
    QLabel *balance_;
    QLineEdit *rechargeEdit_;
    QPushButton *rechargeButton_;
    QLabel *status_;
    QLabel *error_;
    QPushButton *retryButton_;
    bool connected_ = false;
    bool readPending_ = false;
    bool mutationPending_ = false;
    bool hasUser_ = false;
    bool reconciliationRequired_ = false;
};
