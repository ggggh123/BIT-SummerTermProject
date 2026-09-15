#pragma once

#include "app/AppContext.h"

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(AppContext *context, QWidget *parent = nullptr);
    QString adminToken() const;

private slots:
    void tryLogin();

private:
    AppContext *m_context = nullptr;
    bool m_busy = false;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLabel *m_feedbackLabel = nullptr;
    QPushButton *m_loginButton = nullptr;
    QString m_adminToken;
};
