#include "ui/LoginDialog.h"
#include "protocol/JsonEnvelope.h"
#include <QUuid>

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

LoginDialog::LoginDialog(AppContext *context, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
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

QString LoginDialog::adminToken() const
{
    return m_adminToken;
}

void LoginDialog::tryLogin()
{
    if (m_busy) return;
    m_busy = true;
    setEnabled(false);
    m_context->executeLocal({1,QUuid::createUuid().toString(QUuid::WithoutBraces),"admin.login",{},
        {{"username",m_usernameEdit->text()},{"password",m_passwordEdit->text()}}},this,[this](const QByteArray &bytes) {
        m_busy = false;
        setEnabled(true);
        const auto result = ev::protocol::parseResponse(bytes);
        if (!result.ok) {
            QMessageBox::warning(this,QStringLiteral("登录失败"),result.message);
            return;
        }
        m_adminToken = result.data.toObject().value("token").toString();
        accept();
    });
}
