#include "ui/LoginDialog.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

LoginDialog::LoginDialog(AuthService *authService, QWidget *parent)
    : QDialog(parent)
    , m_authService(authService)
{
    setWindowTitle(QStringLiteral("管理员登录"));
    setMinimumWidth(360);

    auto *title = new QLabel(QStringLiteral("充电桩管理平台"));
    QFont titleFont = title->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    title->setFont(titleFont);

    m_usernameEdit = new QLineEdit(QStringLiteral("admin"));
    m_passwordEdit = new QLineEdit;
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("默认密码：123456"));

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("账号"), m_usernameEdit);
    form->addRow(QStringLiteral("密码"), m_passwordEdit);

    auto *loginButton = new QPushButton(QStringLiteral("登录"));
    connect(loginButton, &QPushButton::clicked, this, &LoginDialog::tryLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginDialog::tryLogin);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addLayout(form);
    layout->addWidget(loginButton);
}

void LoginDialog::tryLogin()
{
    const Result result = m_authService->login(m_usernameEdit->text(), m_passwordEdit->text());
    if (!result.ok) {
        QMessageBox::warning(this, QStringLiteral("登录失败"), result.message);
        return;
    }
    accept();
}

