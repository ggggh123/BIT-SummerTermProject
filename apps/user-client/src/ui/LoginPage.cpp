#include "ui/LoginPage.h"

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

namespace {

class AspectRatioPixmapLabel final : public QLabel
{
public:
    explicit AspectRatioPixmapLabel(const QString &resourcePath, QWidget *parent = nullptr)
        : QLabel(parent)
        , source_(resourcePath)
    {
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {350, 211};
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        if (source_.isNull()) {
            QLabel::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize target = source_.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect targetRect((width() - target.width()) / 2,
                               (height() - target.height()) / 2,
                               target.width(), target.height());
        painter.drawPixmap(targetRect, source_);
    }

private:
    QPixmap source_;
};

void refreshStyle(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
    , phoneEdit_(new QLineEdit(this))
    , loginButton_(new QPushButton(QStringLiteral("登录 / 注册"), this))
    , connectionBanner_(new QLabel(this))
    , errorMessage_(new QLabel(this))
{
    setObjectName(QStringLiteral("loginPage"));

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("loginScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("loginContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 20);
    contentLayout->setSpacing(0);

    auto *hero = new QFrame(content);
    hero->setObjectName(QStringLiteral("loginHero"));
    hero->setProperty("role", QStringLiteral("loginHero"));
    hero->setMinimumHeight(270);
    hero->setMaximumHeight(360);
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(0, 16, 0, 0);
    heroLayout->setSpacing(6);

    auto *brand = new QLabel(QStringLiteral("电动汽车充电"), hero);
    brand->setObjectName(QStringLiteral("loginBrand"));
    brand->setProperty("role", QStringLiteral("sectionTitle"));
    brand->setContentsMargins(20, 0, 20, 0);
    heroLayout->addWidget(brand);

    auto *tagline = new QLabel(QStringLiteral("找到合适的站点\n从容开启充电"), hero);
    tagline->setObjectName(QStringLiteral("loginTagline"));
    tagline->setProperty("role", QStringLiteral("pageTitle"));
    tagline->setWordWrap(true);
    tagline->setContentsMargins(20, 0, 20, 0);
    heroLayout->addWidget(tagline);

    auto *illustration = new AspectRatioPixmapLabel(
        QStringLiteral(":/ui/login-illustration.png"), hero);
    illustration->setObjectName(QStringLiteral("loginIllustration"));
    illustration->setMinimumHeight(180);
    heroLayout->addWidget(illustration, 1);
    contentLayout->addWidget(hero, 1);

    auto *lower = new QWidget(content);
    lower->setObjectName(QStringLiteral("loginLower"));
    auto *lowerLayout = new QVBoxLayout(lower);
    lowerLayout->setContentsMargins(20, 14, 20, 0);
    lowerLayout->setSpacing(12);

    auto *formCard = new QFrame(lower);
    formCard->setObjectName(QStringLiteral("loginForm"));
    formCard->setProperty("role", QStringLiteral("card"));
    auto *formLayout = new QVBoxLayout(formCard);
    formLayout->setContentsMargins(16, 16, 16, 16);
    formLayout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("欢迎使用"), formCard);
    title->setObjectName(QStringLiteral("loginTitle"));
    title->setProperty("role", QStringLiteral("sectionTitle"));
    formLayout->addWidget(title);

    auto *accent = new QFrame(formCard);
    accent->setObjectName(QStringLiteral("loginTitleAccent"));
    accent->setFixedSize(48, 4);
    accent->setStyleSheet(QStringLiteral("background: #00856A; border-radius: 2px;"));
    formLayout->addWidget(accent, 0, Qt::AlignLeft);

    auto *phoneLabel = new QLabel(QStringLiteral("手机号"), formCard);
    phoneLabel->setObjectName(QStringLiteral("phoneLabel"));
    formLayout->addWidget(phoneLabel);

    connectionBanner_->setObjectName(QStringLiteral("connectionBanner"));
    connectionBanner_->setWordWrap(true);
    connectionBanner_->setAlignment(Qt::AlignCenter);
    connectionBanner_->setProperty("role", QStringLiteral("secondary"));

    phoneEdit_->setObjectName(QStringLiteral("phoneEdit"));
    phoneEdit_->setPlaceholderText(QStringLiteral("请输入11位手机号"));
    phoneEdit_->setInputMethodHints(Qt::ImhDigitsOnly);
    phoneEdit_->setMaxLength(11);
    phoneEdit_->setClearButtonEnabled(true);
    formLayout->addWidget(phoneEdit_);

    loginButton_->setObjectName(QStringLiteral("loginButton"));
    loginButton_->setProperty("role", QStringLiteral("primary"));
    formLayout->addWidget(loginButton_);

    auto *registrationHint = new QLabel(
        QStringLiteral("首次登录将自动创建账户"), formCard);
    registrationHint->setObjectName(QStringLiteral("registrationHint"));
    registrationHint->setProperty("role", QStringLiteral("secondary"));
    registrationHint->setAlignment(Qt::AlignCenter);
    registrationHint->setWordWrap(true);
    formLayout->addWidget(registrationHint);

    errorMessage_->setObjectName(QStringLiteral("loginError"));
    errorMessage_->setWordWrap(true);
    errorMessage_->setProperty("role", QStringLiteral("danger"));
    errorMessage_->setAlignment(Qt::AlignCenter);
    errorMessage_->hide();
    formLayout->addWidget(errorMessage_);

    lowerLayout->addWidget(formCard);
    lowerLayout->addWidget(connectionBanner_);
    contentLayout->addWidget(lower);

    scrollArea->setWidget(content);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea);

    setConnectionAvailable(false);
    const auto submit = [this] {
        if (!pending_) {
            setError({});
            emit loginRequested(phoneEdit_->text());
        }
    };
    connect(loginButton_, &QPushButton::clicked, this, submit);
    connect(phoneEdit_, &QLineEdit::returnPressed, this, submit);
    setTabOrder(phoneEdit_, loginButton_);
}

void LoginPage::setPending(bool pending)
{
    pending_ = pending;
    phoneEdit_->setEnabled(!pending_);
    loginButton_->setEnabled(!pending_);
    loginButton_->setText(
        pending_ ? QStringLiteral("登录中…") : QStringLiteral("登录 / 注册"));
}

void LoginPage::setConnectionAvailable(bool available)
{
    connectionBanner_->setText(available
            ? QStringLiteral("服务器已连接")
            : QStringLiteral("服务器连接不可用"));
    connectionBanner_->setProperty("status", available
        ? QStringLiteral("ok") : QStringLiteral("error"));
    refreshStyle(connectionBanner_);
}

void LoginPage::setError(const QString &message)
{
    errorMessage_->setText(message);
    errorMessage_->setVisible(!message.isEmpty());
}
