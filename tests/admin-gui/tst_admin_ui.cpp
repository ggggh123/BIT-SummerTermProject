#include "app/AppContext.h"
#include "protocol/JsonEnvelope.h"
#include "ui/MainWindow.h"
#include "ui/LoginDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <memory>

class AdminUiTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_directory;
    std::unique_ptr<AppContext> m_context;
    QString m_token;

    void capture(QWidget &widget, const QString &name)
    {
        const QString folder = qEnvironmentVariable("EV_ADMIN_UI_CAPTURE_DIR");
        if (folder.isEmpty()) return;
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(widget.grab().save(folder + "/" + name + ".png"));
    }

private slots:
    void initTestCase()
    {
        m_context = std::make_unique<AppContext>();
        AppContext::Options options;
        options.port = 0;
        options.databasePath = m_directory.filePath("ui.db");
        options.snapshotPath = m_directory.filePath("snapshot.json");
        QVERIFY(QFile::copy(QStringLiteral(EV_TEST_GOLDEN_DB),options.databasePath));
        QVERIFY(m_context->initialize(options).ok);
        QEventLoop loop;
        m_context->executeLocal({1,QUuid::createUuid().toString(),"admin.login",{},
            {{"username","admin"},{"password","123456"}}}, &loop, [&](const QByteArray &bytes) {
            m_token = ev::protocol::parseResponse(bytes).data.toObject().value("token").toString();
            loop.quit();
        });
        QTimer::singleShot(5000,&loop,&QEventLoop::quit);
        loop.exec();
        QVERIFY(!m_token.isEmpty());
    }

    void sidebarNavigationKeepsAllEightPagesAndKeyboardAccess()
    {
        MainWindow window(m_context.get(), m_token);
        window.resize(1280,720);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tabs = window.findChild<QTabWidget *>("adminTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(),8);
        QVERIFY2(!tabs->tabBar()->isVisible(), "桌面侧栏不能与旧顶部页签重复导航");
        for (int index = 0; index < 8; ++index) {
            auto *button = window.findChild<QPushButton *>(QString("adminNav%1").arg(index));
            QVERIFY2(button, "现有页面必须保留可访问的侧栏入口");
            QVERIFY(button->isVisible());
            QVERIFY(button->focusPolicy() != Qt::NoFocus);
            button->setFocus();
            QTest::keyClick(button,Qt::Key_Space);
            QCOMPARE(tabs->currentIndex(),index);
            QVERIFY(button->isChecked());
            QTest::keyClick(button,Qt::Key_Space);
            QVERIFY2(button->isChecked(), "重复激活当前导航项不能取消选中状态");
            for(int other=0;other<8;++other)
                QCOMPARE(window.findChild<QPushButton *>(QString("adminNav%1").arg(other))->isChecked(),other==index);
            const QRect rect(button->mapTo(&window,QPoint()),button->size());
            QVERIFY(window.rect().contains(rect));
        }
        tabs->setCurrentIndex(0); // 保留原页面切换API与双向选中状态。
        QVERIFY(window.findChild<QPushButton *>("adminNav0")->isChecked());
    }

    void databaseTablesCannotPretendToEditValues()
    {
        MainWindow window(m_context.get(),m_token);
        auto *tabs = window.findChild<QTabWidget *>("adminTabs");
        window.show();
        tabs->setCurrentIndex(4);
        auto *table = window.findChild<QTableWidget *>("userManagementTable");
        QTRY_VERIFY(table->rowCount()>0);
        QCOMPARE(table->editTriggers(),QAbstractItemView::EditTriggers(QAbstractItemView::NoEditTriggers));
        const QString value = table->item(0,2)->text();
        const auto area = table->visualItemRect(table->item(0,2));
        QTest::mouseDClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,area.center());
        QVERIFY(table->findChildren<QLineEdit *>().isEmpty());
        QCOMPARE(table->item(0,2)->text(),value);
    }

    void statusPresentationPreservesBusinessCodeAndAddsChineseAccessibleText()
    {
        MainWindow window(m_context.get(),m_token);
        auto *tabs = window.findChild<QTabWidget *>("adminTabs");
        window.show();
        tabs->setCurrentIndex(2);
        auto *table = window.findChild<QTableWidget *>("chargerManagementTable");
        QTRY_VERIFY(table->rowCount()>0);
        const auto *item = table->item(0,5);
        QVERIFY(!item->text().isEmpty()); // 原业务状态码仍可供刷新和测试使用。
        QVERIFY2(!item->data(Qt::AccessibleTextRole).toString().isEmpty(), "状态标签必须提供中文可访问文本");
        QVERIFY(item->data(Qt::AccessibleTextRole).toString() != item->text());
    }

    void compactWindowKeepsFormsAndDangerControlsReachable()
    {
        MainWindow window(m_context.get(),m_token);
        window.resize(1280,720);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.width()<=1280 && window.height()<=720);
        auto *tabs = window.findChild<QTabWidget *>("adminTabs");
        tabs->setCurrentIndex(3);
        QTest::qWait(100);
        auto *scroll = window.findChild<QScrollArea *>("stationFormScroll");
        QVERIFY2(scroll, "新增站点表单需要受约束的滚动区，不能挤出小窗口");
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QPushButton *create = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text()==QStringLiteral("新增站点")) create=button;
        QVERIFY(create);
        scroll->ensureWidgetVisible(create);
        QCoreApplication::processEvents();
        const QRect rect(create->mapTo(scroll->viewport(),QPoint()),create->size());
        QVERIFY(scroll->viewport()->rect().contains(rect));
        tabs->setCurrentIndex(7);
        auto *danger = window.findChild<QWidget *>("adminDangerZone");
        auto *reset = window.findChild<QPushButton *>("demoResetButton");
        QVERIFY(danger && reset);
        QVERIFY(danger->isAncestorOf(reset));
        QVERIFY(window.rect().contains(QRect(reset->mapTo(&window,QPoint()),reset->size())));
    }

    void compactOverviewKeepsRingAndLegendSeparated()
    {
        MainWindow window(m_context.get(),m_token);
        window.resize(1280,720);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *overview=window.findChild<QTabWidget *>("adminTabs")->widget(0);
        QWidget *ring=nullptr;
        for(auto *widget:overview->findChildren<QWidget *>())
            if(widget->accessibleName()==QStringLiteral("充电桩状态分布")) ring=widget;
        QVERIFY(ring);
        const QRect ringRect(ring->mapTo(overview,QPoint()),ring->size());
        for(auto *label:overview->findChildren<QLabel *>()) {
            if(!QStringList({"空闲","已预约","充电中","故障","重启中"}).contains(label->text())) continue;
            const QRect labelRect(label->mapTo(overview,QPoint()),label->size());
            QVERIFY2(ringRect.bottom()+6<labelRect.top(), "紧凑窗口圆环与图例必须保留间距，不能重叠");
        }
    }

    void overviewRangeAndEmptySearchUseRealData()
    {
        MainWindow window(m_context.get(),m_token);
        window.show();
        auto *range=window.findChild<QComboBox *>("revenueRange");
        auto *trend=window.findChild<QTableWidget *>("revenueTrendTable");
        QVERIFY(range && trend);
        QTRY_COMPARE(trend->rowCount(),7);
        range->setCurrentIndex(range->findData(30));
        QTRY_COMPARE(trend->rowCount(),30);
        auto *chart=window.findChild<QWidget *>("adminRevenueChart");
        QVERIFY(chart->accessibleDescription().contains("30"));
        range->setCurrentIndex(range->findData(7));
        QTRY_COMPARE(trend->rowCount(),7);
        window.findChild<QTabWidget *>("adminTabs")->setCurrentIndex(4);
        auto *users=window.findChild<QTableWidget *>("userManagementTable");
        QTRY_VERIFY(users->rowCount()>0);
        auto *search=window.findChild<QLineEdit *>("userSearchEdit");
        search->setText("no-such-mobile");
        QTest::keyClick(search,Qt::Key_Return);
        QTRY_COMPARE(users->rowCount(),0);
        QVERIFY(users->findChild<QLabel *>("tableEmptyState")->isVisible());
        capture(window,"users-empty");
        search->clear();
        QTest::keyClick(search,Qt::Key_Return);
        QTRY_VERIFY(users->rowCount()>0);
        QVERIFY(!users->findChild<QLabel *>("tableEmptyState")->isVisible());
    }

    void compactStationListStillExposesCoordinates()
    {
        MainWindow window(m_context.get(),m_token);
        window.show();
        window.findChild<QTabWidget *>("adminTabs")->setCurrentIndex(3);
        auto *table=window.findChild<QTableWidget *>("stationManagementTable");
        QTRY_VERIFY(table->rowCount()>0);
        const auto *name=table->item(0,1);
        QVERIFY(name->toolTip().contains(QStringLiteral("纬度")));
        QVERIFY(name->toolTip().contains(table->item(0,3)->text()));
        QVERIFY(name->toolTip().contains(table->item(0,4)->text()));
        QCOMPARE(name->data(Qt::AccessibleDescriptionRole).toString(),name->toolTip());
    }

    void destructiveConfirmationDefaultsToChineseCancel()
    {
        MainWindow window(m_context.get(),m_token);
        window.show();
        window.findChild<QTabWidget *>("adminTabs")->setCurrentIndex(4);
        auto *table=window.findChild<QTableWidget *>("userManagementTable");
        QTRY_VERIFY(table->rowCount()>0);
        table->selectRow(0);
        QPushButton *freeze=nullptr;
        for(auto *button:window.findChildren<QPushButton *>())
            if(button->text()==QStringLiteral("冻结")) freeze=button;
        QVERIFY(freeze);
        bool sawDialog=false, defaultCancel=false, chineseCancel=false;
        QTimer observer;
        connect(&observer,&QTimer::timeout,&window,[&] {
            auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if(!dialog) return;
            sawDialog=true;
            defaultCancel=dialog->defaultButton()==dialog->button(QMessageBox::No);
            chineseCancel=dialog->button(QMessageBox::No)->text()==QStringLiteral("取消");
            capture(*dialog,"confirm-freeze");
            dialog->button(QMessageBox::No)->click();
        });
        observer.start(10);
        QTest::mouseClick(freeze,Qt::LeftButton);
        QVERIFY(sawDialog);
        QVERIFY(defaultCancel);
        QVERIFY(chineseCancel);
        QCOMPARE(table->item(0,5)->text(),QStringLiteral("active"));
    }

    void captureAllPages()
    {
        if (qEnvironmentVariableIsEmpty("EV_ADMIN_UI_CAPTURE_DIR")) QSKIP("按需截图，不写入普通测试运行");
        LoginDialog login(m_context.get());
        login.show();
        QVERIFY(QTest::qWaitForWindowExposed(&login));
        capture(login,"login");
        auto *password=login.findChild<QLineEdit *>("adminPassword");
        password->setText("ui-preview-invalid");
        QTest::mouseClick(login.findChild<QPushButton *>("adminLoginButton"),Qt::LeftButton);
        QTRY_VERIFY(!login.findChild<QLabel *>("loginFeedback")->text().isEmpty());
        capture(login,"login-error");
        login.hide();
        MainWindow window(m_context.get(),m_token);
        window.resize(1360,860);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *tabs=window.findChild<QTabWidget *>("adminTabs");
        const QStringList names={"overview","status","chargers","stations","users","service","logs","health"};
        for(int i=0;i<8;++i) {
            tabs->setCurrentIndex(i);
            if(auto *table=tabs->widget(i)->findChild<QTableWidget *>())
                QTRY_VERIFY(table->rowCount()>0);
            QCoreApplication::processEvents();
            capture(window,names.at(i));
        }
        window.resize(1280,720);
        for(int i:{0,3,4,7}) {
            tabs->setCurrentIndex(i);
            QTest::qWait(100);
            capture(window,names.at(i)+"-compact");
        }
        tabs->setCurrentIndex(0);
        window.findChild<QComboBox *>("revenueRange")->setCurrentIndex(1);
        QTRY_COMPARE(window.findChild<QTableWidget *>("revenueTrendTable")->rowCount(),30);
        capture(window,"overview-30-days-compact");
    }
};

QTEST_MAIN(AdminUiTest)
#include "tst_admin_ui.moc"
