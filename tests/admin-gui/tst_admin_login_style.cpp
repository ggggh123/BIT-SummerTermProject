#include "app/AppContext.h"
#include "ui/LoginDialog.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class AdminLoginStyleTest : public QObject
{
    Q_OBJECT

private slots:
    void emptyFieldsShowRequiredInputWithoutSendingRequest()
    {
        // 未初始化服务：必填反馈必须由表单直接完成，不进入异步请求。
        AppContext context;
        LoginDialog dialog(&context);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const auto fields=dialog.findChildren<QLineEdit *>();
        auto *button=dialog.findChild<QPushButton *>("adminLoginButton");
        auto *feedback=dialog.findChild<QLabel *>("loginFeedback");
        QVERIFY(button && feedback);
        fields.at(0)->clear();
        QTest::mouseClick(button,Qt::LeftButton);
        QVERIFY(dialog.isEnabled());
        QCOMPARE(feedback->text(),QStringLiteral("请输入管理员账号。"));
        QVERIFY(fields.at(0)->hasFocus());
        fields.at(0)->setText("admin");
        QTest::mouseClick(button,Qt::LeftButton);
        QVERIFY(dialog.isEnabled());
        QCOMPARE(feedback->text(),QStringLiteral("请输入登录密码。"));
        QVERIFY(fields.at(1)->hasFocus());
        QVERIFY(dialog.adminToken().isEmpty());
    }

    void passwordRemainsMaskedWithoutDisplayingCredentials()
    {
        AppContext context;
        LoginDialog dialog(&context);
        const auto fields = dialog.findChildren<QLineEdit *>();
        QCOMPARE(fields.size(), 2);
        QCOMPARE(fields.at(1)->echoMode(), QLineEdit::Password);
        QVERIFY(fields.at(1)->text().isEmpty());
        for (const auto *field : fields) {
            QVERIFY2(!field->placeholderText().contains(QStringLiteral("123456")),
                     "The login screen must not advertise a password in its field hint.");
        }
        for (const auto *label : dialog.findChildren<QLabel *>()) {
            QVERIFY(!label->text().contains(QStringLiteral("123456")));
        }
    }

    void formFitsCompactDesktopAndSupportsKeyboardNavigation()
    {
        AppContext context;
        LoginDialog dialog(&context);
        dialog.resize(880, 560);
        dialog.show();
        QApplication::processEvents();

        const auto fields = dialog.findChildren<QLineEdit *>();
        QCOMPARE(fields.size(), 2);
        auto *username = fields.at(0);
        auto *password = fields.at(1);
        QVERIFY2(!username->accessibleName().isEmpty(), "The account field needs an accessible label.");
        QVERIFY2(!password->accessibleName().isEmpty(), "The password field needs an accessible label.");
        QCOMPARE(dialog.size(), QSize(880, 560));
        for (const auto *field : fields) {
            QVERIFY(field->width() >= 280);
            QVERIFY(field->height() >= 40);
            QVERIFY(dialog.rect().contains(QRect(field->mapTo(&dialog, QPoint()), field->size())));
        }
        QVERIFY(username->mapTo(&dialog, QPoint()).y() + username->height()
                < password->mapTo(&dialog, QPoint()).y());

        username->setFocus();
        QTRY_VERIFY(username->hasFocus());
        QTest::keyClick(username, Qt::Key_Tab);
        QVERIFY(password->hasFocus());
        QTest::keyClick(password, Qt::Key_Tab);
        auto *button = qobject_cast<QPushButton *>(QApplication::focusWidget());
        QVERIFY2(button, "Tab from the password field must reach the login action.");
        QVERIFY(dialog.isAncestorOf(button));
        QVERIFY(button->isDefault());
        QVERIFY(button->isEnabled());
    }

    void failedAuthenticationShowsInlineFeedbackAndAllowsEnterToRetry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppContext context;
        AppContext::Options options;
        options.port = 0;
        options.databasePath = directory.filePath(QStringLiteral("login.db"));
        options.snapshotPath = directory.filePath(QStringLiteral("snapshot.json"));
        const auto initialized = context.initialize(options);
        QVERIFY2(initialized.ok, qPrintable(initialized.message));

        LoginDialog dialog(&context);
        dialog.show();
        const auto fields = dialog.findChildren<QLineEdit *>();
        QCOMPARE(fields.size(), 2);
        fields.at(0)->setText(QStringLiteral("admin"));
        auto *password = fields.at(1);
        const QString wrongPassword = QStringLiteral("private-wrong-password-738");
        password->setText(wrongPassword);
        password->setFocus();
        QSignalSpy accepted(&dialog, &QDialog::accepted);

        // 关闭旧版阻塞弹窗，使 RED 阶段也能完成并报告真实的交互退化。
        bool sawModalFailure = false;
        QTimer modalObserver;
        connect(&modalObserver, &QTimer::timeout, &dialog, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                sawModalFailure = true;
                message->accept();
            }
        });
        modalObserver.start(10);
        QTest::keyClick(password, Qt::Key_Return);
        QVERIFY(!dialog.isEnabled());
        QTRY_VERIFY(dialog.isEnabled());
        QCOMPARE(accepted.count(), 0);
        QVERIFY2(!sawModalFailure, "A failed login should keep the form available with inline feedback.");

        auto *feedback = dialog.findChild<QLabel *>(QStringLiteral("loginFeedback"));
        QVERIFY(feedback);
        QVERIFY(feedback->isVisible());
        QVERIFY(!feedback->text().trimmed().isEmpty());
        QVERIFY(!feedback->text().contains(wrongPassword));
        QVERIFY(password->hasFocus());
        QCOMPARE(password->echoMode(), QLineEdit::Password);

        password->setText(QStringLiteral("123456"));
        QTest::keyClick(password, Qt::Key_Return);
        QTRY_COMPARE(accepted.count(), 1);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!dialog.adminToken().isEmpty());
        QVERIFY(!sawModalFailure);
    }
};

QTEST_MAIN(AdminLoginStyleTest)
#include "tst_admin_login_style.moc"
