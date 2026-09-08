#include "app/AppContext.h"
#include "protocol/JsonEnvelope.h"
#include "ui/MainWindow.h"
#include "ui/LoginDialog.h"
#include "ui/EnergyPage.h"
#include "ui/OperationsPages.h"
#include "ui/PulseChart.h"
#include "../fixtures/PulseSamples.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStackedWidget>
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
        window.findChild<QPushButton *>("dashboardModeButton")->click();
        QCoreApplication::processEvents();
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

    void energySamplesAndDeviceSelectionStayLinked()
    {
        MainWindow window(m_context.get(),m_token);
        window.resize(1360,860);
        window.show();
        auto *page = window.findChild<EnergyPage *>("adminEnergyPage");
        QVERIFY(page);
        QTRY_VERIFY(page->findChild<QComboBox *>("energyStationFilter")->count()>0);
        const auto fixtureStart=QDateTime::currentDateTimeUtc().addSecs(-1600);
        page->setData(PulseFixture::station(fixtureStart));
        auto *chart = page->findChild<ev::ui::PulseChart *>("adminPowerChart");
        QCOMPARE(chart->sampleCount(),34);
        QCOMPARE(page->findChild<QLabel *>("energySelectedCode")->text(),QStringLiteral("A-01"));
        QCOMPARE(page->findChild<QLabel *>("energySelectedPower")->text(),QStringLiteral("22.5"));
        QCoreApplication::processEvents();
        for(int i=0;i<8;++i) {
            auto *device=page->findChild<QPushButton *>(QString("energyDevice%1").arg(i));
            QVERIFY(device->height()>=56);
            auto *caption=device->findChild<QLabel *>("energyDeviceCaption");
            QVERIFY(caption->height()>=caption->fontMetrics().height());
        }
        capture(window,"pulse-admin-1360x860");
        QTest::mouseClick(chart,Qt::LeftButton,Qt::NoModifier,QPoint(chart->width()/2,100));
        QVERIFY(!chart->followingLatest());
        QCOMPARE(page->findChild<QLabel *>("energySelectedPower")->text(),QString::number(chart->reading(0)->kw,'f',1));
        const double held=chart->cursorSeconds();
        page->setData(PulseFixture::station(fixtureStart.addSecs(100)));
        QVERIFY(qAbs(chart->cursorSeconds()-(held-100))<0.001);
        page->setData(PulseFixture::station(fixtureStart));
        page->findChild<QPushButton *>("energyFollowLatest")->click();
        page->findChild<QPushButton *>("energyDevice5")->click();
        QCOMPARE(page->findChild<QLabel *>("energySelectedCode")->text(),QStringLiteral("A-06"));
        QCOMPARE(page->findChild<QLabel *>("energySelectedPower")->text(),QStringLiteral("18.0"));
        page->findChild<QPushButton *>("energyDevice1")->click();
        QCOMPARE(page->findChild<QLabel *>("energySelectedPower")->text(),QStringLiteral("—"));
        QVERIFY(chart->sampleCount()>0); // 无样本设备不抹掉另一台设备的有效曲线。
        page->findChild<QPushButton *>("energyDevice0")->click();
        window.resize(1280,720);
        QCoreApplication::processEvents();
        auto *viewport = window.findChild<QScrollArea *>("energyPageViewport");
        QCOMPARE(viewport->horizontalScrollBar()->maximum(),0);
        auto *latest = page->findChild<QPushButton *>("energyFollowLatest");
        viewport->ensureWidgetVisible(latest);
        QVERIFY(viewport->viewport()->rect().contains(QRect(latest->mapTo(viewport->viewport(),QPoint()),latest->size())));
        viewport->verticalScrollBar()->setValue(0);
        capture(window,"pulse-admin-1280x720");
    }

    void fleetFiltersAndHealthNavigationUseTheSameDevice()
    {
        MainWindow window(m_context.get(),m_token); window.show();
        auto *tabs=window.findChild<QTabWidget *>("adminTabs");
        auto *fleet=window.findChild<FleetStatusPage *>();
        auto *health=window.findChild<SystemHealthPage *>();
        QVERIFY(fleet&&health);
        tabs->setCurrentIndex(7);
        auto *faults=health->findChild<QListWidget *>("healthFaultList");
        QTRY_VERIFY(faults->count()>0);
        QCOMPARE(health->findChild<QLabel *>("healthListenValue")->text(),QStringLiteral("监听中"));
        QCOMPARE(health->findChild<QLabel *>("healthDatabaseValue")->text(),QStringLiteral("可读取"));
        QCOMPARE(health->findChild<QTableWidget *>("healthOptionalTable")->item(1,1)->text(),QString("unverified"));
        faults->setCurrentRow(0);
        const int id=faults->currentItem()->data(Qt::UserRole).toInt();
        auto *locate=health->findChild<QPushButton *>("healthLocateFault");
        QVERIFY(locate->isEnabled());
        QCoreApplication::processEvents();
        capture(window,"health-selected-1360x860");
        locate->click();
        QCOMPARE(tabs->currentIndex(),1);
        QTRY_COMPARE(fleet->selectedChargerId(),id);
        auto *selected=fleet->findChild<QPushButton *>(QString("fleetDevice%1").arg(id));
        QVERIFY(selected);
        QTRY_VERIFY(selected->isVisible());
        QCoreApplication::processEvents();
        auto *array=fleet->findChild<QScrollArea *>("fleetArrayViewport");
        QTRY_VERIFY(array->viewport()->rect().contains(QRect(selected->mapTo(array->viewport(),QPoint()),selected->size())));
        auto *restart=fleet->findChild<QPushButton *>("fleetRestartButton");
        QVERIFY(restart->isEnabled());
        capture(window,"fleet-selected-1360x860");
        fleet->findChild<QPushButton *>("fleetFilter_fault")->click();
        QCOMPARE(fleet->findChild<QTableWidget *>("pileStatusDetailTable")->rowCount(),faults->count());
        auto *station=fleet->findChild<QComboBox *>("fleetStationFilter");
        station->setCurrentIndex(station->findData(1)); // 黄金库故障位于站点 2。
        QCOMPARE(fleet->findChild<QTableWidget *>("pileStatusDetailTable")->rowCount(),0);
        QCOMPARE(fleet->selectedChargerId(),0);
        QVERIFY(!restart->isEnabled());
        QVERIFY(fleet->findChild<QLabel *>("fleetEmptyState")->isVisible());
        station->setCurrentIndex(0);
        fleet->findChild<QPushButton *>("fleetFilter_all")->click();
        auto *toggle=fleet->findChild<QPushButton *>("fleetViewToggle"); toggle->click();
        QVERIFY(fleet->findChild<QTableWidget *>("pileStatusDetailTable")->isVisible());
        capture(window,"fleet-list-1360x860");
        toggle->click();
        window.resize(1280,720); QCoreApplication::processEvents();
        auto *viewport=window.findChild<QScrollArea *>("fleetPageViewport");
        QCOMPARE(window.size(),QSize(1280,720));
        QCOMPARE(viewport->horizontalScrollBar()->maximum(),0);
        QCOMPARE(fleet->findChild<QScrollArea *>("fleetArrayViewport")->horizontalScrollBar()->maximum(),0);
        // 样式表的按钮高度不应挤破网格行；小窗口必须滚动而非重叠。
        for(auto *group:fleet->findChildren<QFrame *>()) {
            if(group->property("role").toString()!="stationGroup")continue;
            const auto tiles=group->findChildren<QPushButton *>();
            for(int i=0;i<tiles.size();++i) {
                QVERIFY(tiles[i]->height()>=56);
                for(auto *label:tiles[i]->findChildren<QLabel *>())
                    QVERIFY(label->height()>=label->fontMetrics().height());
                for(int j=i+1;j<tiles.size();++j) QVERIFY(!tiles[i]->geometry().intersects(tiles[j]->geometry()));
            }
        }
        viewport->ensureWidgetVisible(restart);
        QVERIFY(viewport->viewport()->rect().contains(QRect(restart->mapTo(viewport->viewport(),QPoint()),restart->size())));
        capture(window,"fleet-compact-1280x720");
        tabs->setCurrentIndex(7);
        QTRY_VERIFY(!health->property("operationsPending").toBool());
        health->findChild<QPushButton *>("healthDetailsToggle")->click();
        QCOMPARE(health->findChild<QStackedWidget *>("healthDetailsViews")->currentIndex(),1);
        capture(window,"health-details-1280x720");
        auto *reset=health->resetButton();
        QVERIFY(window.rect().contains(QRect(reset->mapTo(&window,QPoint()),reset->size())));
    }

    void operationsReadViewRequiresAdminAndKeepsStationIds()
    {
        auto read=[this](const QString &token) {
            QByteArray result;
            QEventLoop loop;
            m_context->queryAdmin(AdminView::Operations,token,{},&loop,[&](const QByteArray &bytes){result=bytes;loop.quit();});
            QTimer::singleShot(3000,&loop,&QEventLoop::quit);
            loop.exec();
            return result;
        };
        const auto denied=ev::protocol::parseResponse(read(QString()));
        QVERIFY(!denied.ok); QCOMPARE(denied.code,QString("AUTH_REQUIRED"));
        const auto accepted=ev::protocol::parseResponse(read(m_token));
        QVERIFY(accepted.ok);
        const auto data=accepted.data.toObject();
        QCOMPARE(data.value("stations").toArray().size(),6);
        QCOMPARE(data.value("chargers").toArray().size(),48);
        QVERIFY(data.contains("latestTelemetry"));
        QVERIFY(!data.value("readAt").toString().isEmpty());
        for(const auto &entry:data.value("chargers").toArray()) {
            const auto device=entry.toObject();
            QVERIFY(device.value("stationId").toInt()>0);
            QVERIFY(device.contains("ratedPowerKw"));
        }
        FleetStatusPage page;
        // 重名站点和不同设备 ID 必须保留独立分组，不能以名字当主键。
        const QJsonObject duplicate{{"stations",QJsonArray{
            QJsonObject{{"id",11},{"name",QStringLiteral("同名站点")}},
            QJsonObject{{"id",12},{"name",QStringLiteral("同名站点")}}}},
            {"chargers",QJsonArray{
            QJsonObject{{"id",51},{"code","A-01"},{"stationId",11},{"stationName",QStringLiteral("同名站点")},{"status","fault"},{"ratedPowerKw",60}},
            QJsonObject{{"id",52},{"code","A-02"},{"stationId",12},{"stationName",QStringLiteral("同名站点")},{"status","idle"},{"ratedPowerKw",30}}}}};
        page.setData(duplicate);
        QVERIFY(page.findChild<QWidget *>("fleetStation11"));
        QVERIFY(page.findChild<QWidget *>("fleetStation12"));
        page.selectCharger(51);
        QCOMPARE(page.selectedChargerId(),51);
        auto *restart=page.findChild<QPushButton *>("fleetRestartButton");
        QVERIFY(restart->isEnabled());
        page.setRestartPending(true);
        page.setData(duplicate);
        QVERIFY(!restart->isEnabled());
        page.setRestartPending(false);
        QVERIFY(!restart->isEnabled()); // ACK 与新快照之间也不能重复发控制。
        page.setData(duplicate);
        QVERIFY(restart->isEnabled());
        page.setReadError(QStringLiteral("测试读取失败"));
        QVERIFY(!restart->isEnabled());
        page.setData(QJsonObject{{"stations",QJsonArray{}},{"chargers",QJsonArray{}}});
        QCOMPARE(page.selectedChargerId(),0);
        QCOMPARE(page.findChild<QLabel *>("fleetTotal")->text(),QString("0"));
        QVERIFY(!restart->isEnabled());
    }

    void fleetRestartConfirmationAndAuthoritativeTransition()
    {
        QTemporaryDir directory;
        AppContext context;
        AppContext::Options options; options.port=0;
        options.databasePath=directory.filePath("restart.db");
        options.snapshotPath=directory.filePath("snapshot.json");
        QVERIFY(QFile::copy(QStringLiteral(EV_TEST_GOLDEN_DB),options.databasePath));
        QVERIFY(context.initialize(options).ok);
        QString token;
        QEventLoop login;
        context.executeLocal({1,QUuid::createUuid().toString(),"admin.login",{},{{"username","admin"},{"password","123456"}}},
            &login,[&](const QByteArray &bytes){token=ev::protocol::parseResponse(bytes).data.toObject().value("token").toString();login.quit();});
        QTimer::singleShot(3000,&login,&QEventLoop::quit);login.exec();QVERIFY(!token.isEmpty());
        MainWindow window(&context,token);window.show();
        window.findChild<QTabWidget *>("adminTabs")->setCurrentIndex(1);
        auto *page=window.findChild<FleetStatusPage *>();
        QTRY_VERIFY(page->findChild<QPushButton *>("fleetDevice10"));
        page->selectCharger(10);
        auto *button=page->findChild<QPushButton *>("fleetRestartButton");
        auto *state=page->findChild<QLabel *>("fleetSelectedState");
        QVERIFY(button->isEnabled());
        bool cancelled=false, defaultNo=false;
        QTimer::singleShot(0,&window,[&] {
            auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if(!box)return;
            defaultNo=box->defaultButton()==box->button(QMessageBox::No);
            box->button(QMessageBox::No)->click();cancelled=true;
        });
        button->click();
        QVERIFY(cancelled&&defaultNo);
        QCOMPARE(state->text(),QStringLiteral("故障"));
        const QString dbName=QUuid::createUuid().toString();
        auto db=QSqlDatabase::addDatabase("QSQLITE",dbName);db.setDatabaseName(options.databasePath);QVERIFY(db.open());
        auto requests=[&]{
            QSqlQuery query(db);
            if(!query.exec("SELECT COUNT(*) FROM request_log WHERE action='admin.charger_restart'")||!query.next())return -1;
            return query.value(0).toInt();
        };
        QCOMPARE(requests(),0);
        QTimer::singleShot(0,&window,[] {
            if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->button(QMessageBox::Yes)->click();
        });
        button->click();
        QVERIFY(!button->isEnabled());
        button->click();
        QTRY_COMPARE(requests(),1);
        QTRY_COMPARE(state->text(),QStringLiteral("重启中"));
        QVERIFY(!button->isEnabled());
        capture(window,"fleet-restarting-1360x860");
        QTRY_COMPARE_WITH_TIMEOUT(state->text(),QStringLiteral("空闲"),5000);
        QVERIFY(!button->isEnabled());
        QCOMPARE(requests(),1);
        db.close();db={};QSqlDatabase::removeDatabase(dbName);
    }

    void operationsEmptyErrorsAndSnapshotCaptures()
    {
        MainWindow window(m_context.get(),m_token);window.show();
        auto *tabs=window.findChild<QTabWidget *>("adminTabs");
        auto *fleet=window.findChild<FleetStatusPage *>();
        auto *health=window.findChild<SystemHealthPage *>();
        QTRY_VERIFY(fleet->findChild<QPushButton *>("fleetDevice10"));
        QTRY_VERIFY(!health->property("operationsPending").toBool());
        for(auto *timer:window.findChildren<QTimer *>())timer->stop();
        const QJsonObject empty{{"stations",QJsonArray{}},{"chargers",QJsonArray{}},
            {"latestTelemetry",QJsonValue(QJsonValue::Null)},{"readAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
        tabs->setCurrentIndex(1);
        QTRY_VERIFY(!fleet->property("operationsPending").toBool());
        fleet->setData(empty); QCoreApplication::processEvents();
        QCOMPARE(fleet->findChild<QLabel *>("fleetTotal")->text(),QString("0"));
        QTRY_COMPARE(fleet->findChild<QScrollArea *>("fleetArrayViewport")->verticalScrollBar()->maximum(),0);
        capture(window,"fleet-empty-1360x860");
        fleet->setReadError("DB_ERROR"); QCoreApplication::processEvents();
        capture(window,"fleet-read-error-1360x860");
        tabs->setCurrentIndex(7);QTRY_VERIFY(!health->property("operationsPending").toBool());
        health->setData(empty,m_context->healthSnapshot(),true,"127.0.0.1:9100");
        QCOMPARE(health->findChild<QLabel *>("healthTelemetryValue")->text(),QStringLiteral("尚无记录"));
        QCOMPARE(health->findChild<QLabel *>("healthFaultCount")->text(),QString("0"));
        QVERIFY(!health->findChild<QPushButton *>("healthLocateFault")->isEnabled());
        QCoreApplication::processEvents();capture(window,"health-empty-1360x860");
        QJsonObject sampled=empty;
        sampled["latestTelemetry"]=QJsonObject{{"chargerId",10},{"recordedAt","2026-09-08T16:00:00+08:00"},{"powerKw",22.5}};
        health->setData(sampled,m_context->healthSnapshot(),false,"127.0.0.1:9100");
        QCOMPARE(health->findChild<QLabel *>("healthListenValue")->text(),QStringLiteral("已停止"));
        QCOMPARE(health->findChild<QLabel *>("healthTelemetryValue")->text(),QStringLiteral("已有记录"));
        QCOMPARE(health->findChild<QTableWidget *>("healthOptionalTable")->item(1,1)->text(),QString("unverified"));
        QCoreApplication::processEvents();capture(window,"health-stopped-1360x860");
        health->setReadError("DB_ERROR");
        QCOMPARE(health->findChild<QLabel *>("healthDatabaseValue")->text(),QStringLiteral("读取失败"));
        QCOMPARE(health->findChild<QLabel *>("healthListenValue")->text(),QStringLiteral("待核对"));
        QCoreApplication::processEvents();capture(window,"health-read-error-1360x860");
    }

    void healthOverviewBackgroundMatchesCard_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("1360x860") << QSize(1360, 860);
        QTest::newRow("1280x720") << QSize(1280, 720);
    }

    void healthOverviewBackgroundMatchesCard()
    {
        QFETCH(QSize, size);
        MainWindow window(m_context.get(), m_token);
        window.resize(size);
        window.show();
        auto *navigation = window.findChild<QPushButton *>("adminNav7");
        navigation->setFocus();
        navigation->click();
        auto *health = window.findChild<SystemHealthPage *>();
        QTRY_VERIFY(!health->property("operationsPending").toBool());
        QCoreApplication::processEvents();
        auto *card = health->findChild<QWidget *>("healthDiagnostics");
        auto *views = health->findChild<QStackedWidget *>("healthDetailsViews");
        QVERIFY(card && views);
        QCOMPARE(views->currentIndex(), 0);
        const auto pixmap = card->grab();
        const auto image = pixmap.toImage();
        const qreal dpr = pixmap.devicePixelRatio();
        const QColor cardColor = image.pixelColor(qRound(10 * dpr), qRound(30 * dpr));
        QCOMPARE(cardColor, QColor("#193444"));
        for (const auto &point : {QPoint(views->width() - 5, 5),
                                  QPoint(views->width() - 5, views->height() - 5)}) {
            const QPoint local = views->mapTo(card, point);
            QCOMPARE(image.pixelColor(qRound(local.x() * dpr), qRound(local.y() * dpr)), cardColor);
        }
        capture(window, QString("health-background-%1x%2").arg(size.width()).arg(size.height()));
        health->findChild<QPushButton *>("healthDetailsToggle")->click();
        QCOMPARE(views->currentIndex(), 1);
        health->findChild<QPushButton *>("healthDetailsToggle")->click();
        QCOMPARE(views->currentIndex(), 0);
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
        window.findChild<QPushButton *>("dashboardModeButton")->click();
        window.findChild<QComboBox *>("revenueRange")->setCurrentIndex(1);
        QTRY_COMPARE(window.findChild<QTableWidget *>("revenueTrendTable")->rowCount(),30);
        capture(window,"overview-30-days-compact");
    }
};

QTEST_MAIN(AdminUiTest)
#include "tst_admin_ui.moc"
