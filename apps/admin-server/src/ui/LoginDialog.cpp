#include "ui/LoginDialog.h"
#include "ui/AdminTheme.h"
#include "protocol/JsonEnvelope.h"

#include <QAccessible>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QUuid>
#include <QVariant>
#include <QVBoxLayout>

namespace {

class ChargingIllustration final : public QWidget
{
public:
    explicit ChargingIllustration(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(142);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAccessibleName(QStringLiteral("充电站示意图"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate((width() - 262) / 2.0, 0);
        painter.setPen(QPen(QColor("#295143"), 1));
        painter.setBrush(QColor("#15372E"));
        painter.drawRoundedRect(QRectF(0.5, 0.5, 261, 141), 14, 14);

        painter.setPen(QPen(QColor("#365F4E"), 1.2));
        painter.drawLine(QPointF(20, 116), QPointF(241, 116));
        painter.drawLine(QPointF(29, 116), QPointF(29, 52));
        painter.drawLine(QPointF(229, 116), QPointF(229, 52));
        painter.setBrush(QColor("#1B4135"));
        painter.drawRoundedRect(QRectF(20, 38, 219, 14), 4, 4);

        painter.setPen(QPen(QColor("#7FB397"), 1.5));
        painter.setBrush(QColor("#234D3D"));
        painter.drawRoundedRect(QRectF(53, 65, 35, 51), 5, 5);
        painter.setBrush(QColor("#102D29"));
        painter.drawRoundedRect(QRectF(60, 72, 21, 17), 3, 3);
        QPainterPath bolt;
        bolt.moveTo(72, 75);
        bolt.lineTo(66, 82);
        bolt.lineTo(71, 82);
        bolt.lineTo(68, 87);
        painter.setPen(QPen(QColor("#AED5AE"), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(bolt);
        painter.setPen(QPen(QColor("#629B7D"), 1.5));
        QPainterPath cable;
        cable.moveTo(89, 78);
        cable.cubicTo(106, 78, 96, 105, 111, 105);
        cable.cubicTo(117, 105, 120, 100, 120, 96);
        painter.drawPath(cable);

        QPainterPath car;
        car.moveTo(124, 109);
        car.lineTo(122, 94);
        car.quadTo(122, 88, 132, 86);
        car.lineTo(142, 73);
        car.quadTo(146, 68, 155, 68);
        car.lineTo(178, 68);
        car.quadTo(185, 68, 190, 76);
        car.lineTo(199, 88);
        car.quadTo(211, 89, 211, 98);
        car.lineTo(210, 109);
        car.closeSubpath();
        painter.setPen(QPen(QColor("#709F7F"), 1.5));
        painter.setBrush(QColor("#2A523D"));
        painter.drawPath(car);
        QPainterPath windows;
        windows.moveTo(139, 85);
        windows.lineTo(149, 74);
        windows.lineTo(175, 74);
        windows.quadTo(181, 74, 184, 79);
        windows.lineTo(189, 86);
        windows.closeSubpath();
        painter.setPen(QPen(QColor("#496E51"), 1));
        painter.setBrush(QColor("#15372E"));
        painter.drawPath(windows);
        painter.drawLine(QPointF(165, 74), QPointF(165, 85));
        painter.setPen(QPen(QColor("#9ABEA0"), 1.5));
        painter.setBrush(QColor("#102D29"));
        painter.drawEllipse(QRectF(134, 103, 14, 14));
        painter.drawEllipse(QRectF(187, 103, 14, 14));
        painter.setPen(QPen(QColor("#91B593"), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(125, 96), QPointF(130, 96));
        painter.drawLine(QPointF(204, 96), QPointF(208, 96));

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#8FB99D"));
        painter.drawEllipse(QRectF(205, 18, 4, 4));
        painter.setBrush(QColor("#3C6952"));
        painter.drawEllipse(QRectF(194, 19, 2, 2));
        painter.drawEllipse(QRectF(215, 24, 2, 2));
    }
};

QLabel *label(const QString &text, const char *objectName, QWidget *parent)
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(QString::fromLatin1(objectName));
    result->setTextFormat(Qt::PlainText);
    return result;
}

} // namespace

LoginDialog::LoginDialog(AppContext *context, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
{
    if (auto *application = qobject_cast<QApplication *>(QCoreApplication::instance()))
        AdminTheme::apply(*application);
    setObjectName(QStringLiteral("adminLogin"));
    setWindowTitle(QStringLiteral("充电运营平台 · 管理员登录"));
    setMinimumSize(820, 540);
    resize(880, 560);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *brandPanel = new QWidget(this);
    brandPanel->setObjectName(QStringLiteral("loginBrandPanel"));
    brandPanel->setProperty("role", "sidebar");
    brandPanel->setFixedWidth(330);
    auto *brandLayout = new QVBoxLayout(brandPanel);
    brandLayout->setContentsMargins(34, 32, 34, 26);
    brandLayout->setSpacing(0);
    auto *brandRow = new QHBoxLayout;
    brandRow->setSpacing(11);
    auto *brandMark = new QLabel(brandPanel);
    brandMark->setFixedSize(36, 40);
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setPixmap(AdminTheme::icon(QStringLiteral("bolt"), QColor("#D7EED3")).pixmap(25, 25));
    brandMark->setStyleSheet(QStringLiteral("background: #214B3C; border-radius: 9px;"));
    auto *brandName = new QVBoxLayout;
    brandName->setContentsMargins(0, 0, 0, 0);
    brandName->setSpacing(1);
    brandName->addWidget(label(QStringLiteral("充电运营平台"), "loginBrandName", brandPanel));
    brandName->addWidget(label(QStringLiteral("CHARGING OPERATIONS"), "loginBrandOverline", brandPanel));
    brandRow->addWidget(brandMark);
    brandRow->addLayout(brandName);
    brandRow->addStretch();
    brandLayout->addLayout(brandRow);
    brandLayout->addSpacing(28);
    brandLayout->addWidget(label(QStringLiteral("连接每一站\n掌握每一程"), "loginBrandTitle", brandPanel));
    brandLayout->addSpacing(13);
    auto *brandDescription = label(QStringLiteral("让充电网络的日常运营\n清晰、有序、高效。"), "loginBrandDescription", brandPanel);
    brandDescription->setWordWrap(true);
    brandLayout->addWidget(brandDescription);
    brandLayout->addSpacing(20);
    brandLayout->addWidget(new ChargingIllustration(brandPanel));
    brandLayout->addStretch(1);
    brandLayout->addSpacing(12);
    brandLayout->addWidget(label(QStringLiteral("站点管理   /   设备运维   /   运营分析"), "loginBrandFooter", brandPanel));

    auto *formPanel = new QWidget(this);
    formPanel->setObjectName(QStringLiteral("loginFormPanel"));
    auto *formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(50, 32, 50, 26);
    formLayout->setSpacing(0);
    formLayout->addStretch(1);
    formLayout->addWidget(label(QStringLiteral("ADMIN CONSOLE"), "loginEyebrow", formPanel));
    formLayout->addSpacing(8);
    auto *title = label(QStringLiteral("管理员登录"), "loginTitle", formPanel);
    title->setProperty("role", "pageTitle");
    formLayout->addWidget(title);
    formLayout->addSpacing(6);
    auto *subtitle = label(QStringLiteral("欢迎回来，继续管理您的充电网络。"), "loginSubtitle", formPanel);
    subtitle->setProperty("role", "pageSubtitle");
    formLayout->addWidget(subtitle);
    formLayout->addSpacing(24);

    m_usernameEdit = new QLineEdit(QStringLiteral("admin"), formPanel);
    m_usernameEdit->setObjectName(QStringLiteral("adminUsername"));
    m_usernameEdit->setAccessibleName(QStringLiteral("管理员账号"));
    m_usernameEdit->setPlaceholderText(QStringLiteral("请输入管理员账号"));
    m_usernameEdit->setClearButtonEnabled(true);
    m_passwordEdit = new QLineEdit(formPanel);
    m_passwordEdit->setObjectName(QStringLiteral("adminPassword"));
    m_passwordEdit->setAccessibleName(QStringLiteral("登录密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));

    auto *usernameLabel = label(QStringLiteral("管理员账号"), "loginFieldLabel", formPanel);
    usernameLabel->setBuddy(m_usernameEdit);
    formLayout->addWidget(usernameLabel);
    formLayout->addSpacing(8);
    formLayout->addWidget(m_usernameEdit);
    formLayout->addSpacing(16);
    auto *passwordLabel = label(QStringLiteral("登录密码"), "loginFieldLabel", formPanel);
    passwordLabel->setBuddy(m_passwordEdit);
    formLayout->addWidget(passwordLabel);
    formLayout->addSpacing(8);
    formLayout->addWidget(m_passwordEdit);

    m_feedbackLabel = label(QString(), "loginFeedback", formPanel);
    m_feedbackLabel->setAccessibleName(QStringLiteral("登录反馈"));
    m_feedbackLabel->setWordWrap(true);
    m_feedbackLabel->setMinimumHeight(30);
    formLayout->addWidget(m_feedbackLabel);
    m_loginButton = new QPushButton(QStringLiteral("登录管理控制台"), formPanel);
    m_loginButton->setObjectName(QStringLiteral("adminLoginButton"));
    m_loginButton->setProperty("role", "primary");
    m_loginButton->setAccessibleName(QStringLiteral("登录管理控制台"));
    m_loginButton->setDefault(true);
    m_loginButton->setCursor(Qt::PointingHandCursor);
    formLayout->addWidget(m_loginButton);
    formLayout->addSpacing(18);

    auto *environmentRow = new QHBoxLayout;
    environmentRow->setContentsMargins(0, 0, 0, 0);
    environmentRow->setSpacing(6);
    auto *shield = new QLabel(formPanel);
    shield->setPixmap(AdminTheme::icon(QStringLiteral("shield")).pixmap(15, 15));
    shield->setFixedSize(16, 20);
    auto *environment = label(QStringLiteral("演示环境 · 仅限授权管理人员访问"), "loginEnvironment", formPanel);
    environment->setProperty("role", "secondary");
    environmentRow->addStretch();
    environmentRow->addWidget(shield);
    environmentRow->addWidget(environment);
    environmentRow->addStretch();
    formLayout->addLayout(environmentRow);
    formLayout->addStretch(1);

    layout->addWidget(brandPanel);
    layout->addWidget(formPanel, 1);
    setTabOrder(m_usernameEdit, m_passwordEdit);
    setTabOrder(m_passwordEdit, m_loginButton);
    m_usernameEdit->setFocus();
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::tryLogin);
    connect(m_usernameEdit, &QLineEdit::returnPressed, this, &LoginDialog::tryLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginDialog::tryLogin);
}

QString LoginDialog::adminToken() const
{
    return m_adminToken;
}

void LoginDialog::tryLogin()
{
    if (m_busy) return;
    QLineEdit *missing=m_usernameEdit->text().trimmed().isEmpty() ? m_usernameEdit
        : m_passwordEdit->text().isEmpty() ? m_passwordEdit : nullptr;
    if(missing) {
        const QString feedback=missing==m_usernameEdit ? QStringLiteral("请输入管理员账号。")
            : QStringLiteral("请输入登录密码。");
        m_feedbackLabel->setText(feedback);
        m_feedbackLabel->setAccessibleDescription(feedback);
        missing->setFocus();
        QAccessibleEvent event(m_feedbackLabel,QAccessible::Alert);
        QAccessible::updateAccessibility(&event);
        return;
    }
    m_busy = true;
    m_feedbackLabel->clear();
    m_feedbackLabel->setAccessibleDescription(QString());
    m_loginButton->setText(QStringLiteral("正在登录…"));
    setEnabled(false);
    m_context->executeLocal({1,QUuid::createUuid().toString(QUuid::WithoutBraces),"admin.login",{},
        {{"username",m_usernameEdit->text()},{"password",m_passwordEdit->text()}}},this,[this](const QByteArray &bytes) {
        m_busy = false;
        setEnabled(true);
        m_loginButton->setText(QStringLiteral("登录管理控制台"));
        const auto result = ev::protocol::parseResponse(bytes);
        if (!result.ok) {
            const QString feedback = result.code == QStringLiteral("INVALID_CREDENTIALS")
                ? QStringLiteral("账号或密码错误，请重新输入。")
                : QStringLiteral("登录暂未完成，请稍后重试。");
            m_feedbackLabel->setText(feedback);
            m_feedbackLabel->setAccessibleDescription(feedback);
            m_passwordEdit->setFocus();
            m_passwordEdit->selectAll();
            QAccessibleEvent event(m_feedbackLabel, QAccessible::Alert);
            QAccessible::updateAccessibility(&event);
            return;
        }
        m_adminToken = result.data.toObject().value("token").toString();
        accept();
    });
}
