#include "ui/LoginPage.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
    , phoneEdit_(new QLineEdit(this))
    , loginButton_(new QPushButton(QStringLiteral("登录"), this))
    , connectionBanner_(new QLabel(this))
    , errorMessage_(new QLabel(this))
{
    setObjectName(QStringLiteral("loginPage"));
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("手机号登录"), this);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    connectionBanner_->setObjectName(QStringLiteral("connectionBanner"));
    connectionBanner_->setWordWrap(true);
    layout->addWidget(connectionBanner_);

    phoneEdit_->setObjectName(QStringLiteral("phoneEdit"));
    phoneEdit_->setPlaceholderText(QStringLiteral("请输入11位手机号"));
    phoneEdit_->setInputMethodHints(Qt::ImhDigitsOnly);
    layout->addWidget(phoneEdit_);

    loginButton_->setObjectName(QStringLiteral("loginButton"));
    layout->addWidget(loginButton_);

    errorMessage_->setObjectName(QStringLiteral("loginError"));
    errorMessage_->setWordWrap(true);
    errorMessage_->setStyleSheet(QStringLiteral("color: #b00020;"));
    layout->addWidget(errorMessage_);
    layout->addStretch();

    setConnectionAvailable(false);
    connect(loginButton_, &QPushButton::clicked, this, [this] {
        if (!pending_) {
            setError({});
            emit loginRequested(phoneEdit_->text());
        }
    });
}

void LoginPage::setPending(bool pending)
{
    pending_ = pending;
    loginButton_->setEnabled(!pending_);
}

void LoginPage::setConnectionAvailable(bool available)
{
    connectionBanner_->setText(available
            ? QStringLiteral("服务器已连接")
            : QStringLiteral("服务器连接不可用"));
}

void LoginPage::setError(const QString &message)
{
    errorMessage_->setText(message);
}
