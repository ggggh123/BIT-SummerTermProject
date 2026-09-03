#pragma once

#include "services/AuthService.h"

#include <QDialog>

class QLineEdit;

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(AuthService *authService, QWidget *parent = nullptr);

private slots:
    void tryLogin();

private:
    AuthService *m_authService = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
};

