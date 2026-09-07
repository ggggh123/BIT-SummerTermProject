#include "ui/MainWindow.h"
#include "ui/AdminTheme.h"
#include "ui/AdminVisuals.h"
#include "protocol/JsonEnvelope.h"
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QClipboard>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTabBar>
#include <QUuid>
#include <QStatusBar>

#include <QAbstractItemView>
#include <QHeaderView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {

QLabel *textLabel(const QString &text,const char *role="secondary",QWidget *parent=nullptr)
{
    auto *label=new QLabel(text,parent);
    label->setProperty("role",role);
    return label;
}

QFrame *panel(const QString &title={},const QString &description={})
{
    auto *frame=new QFrame;
    frame->setProperty("role","card");
    auto *layout=new QVBoxLayout(frame);
    layout->setContentsMargins(20,18,20,18);
    layout->setSpacing(12);
    if(!title.isEmpty()) layout->addWidget(textLabel(title,"sectionTitle"));
    if(!description.isEmpty()) {
        auto *label=textLabel(description); label->setWordWrap(true); layout->addWidget(label);
    }
    return frame;
}

QVBoxLayout *pageLayout(QWidget *page)
{
    auto *layout=new QVBoxLayout(page);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(16);
    return layout;
}

QString moneyText(qint64 fen)
{
    return QStringLiteral("¥%1").arg(QString::number(fen / 100.0, 'f', 2));
}

QTableWidget *makeTable(const QStringList &headers)
{
    auto *table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(44);
    table->horizontalHeader()->setMinimumSectionSize(60);
    table->setAlternatingRowColors(false);
    table->setShowGrid(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setItemDelegate(new AdminVisuals::StatusDelegate(table));
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);
    table->setMinimumHeight(84);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    if(headers.first().contains(QStringLiteral("ID")) && headers.size()>2) {
        table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
        table->setColumnWidth(0,headers.first().contains(QStringLiteral("请求")) ? 230 : 64);
    }
    auto *empty=new QLabel(QStringLiteral("暂无匹配记录\n可调整筛选条件后重试"),table->viewport());
    empty->setObjectName(QStringLiteral("tableEmptyState"));
    empty->setProperty("role","secondary");
    empty->setAlignment(Qt::AlignCenter);
    empty->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *overlay=new QVBoxLayout(table->viewport());
    overlay->addWidget(empty);
    empty->hide();
    return table;
}

void fillTable(QTableWidget *table, const QList<QStringList> &rows)
{
    QString selectedKey;
    if (table->currentRow() >= 0 && table->item(table->currentRow(), 0)) {
        selectedKey = table->item(table->currentRow(), 0)->text();
    }
    table->clearSelection();
    table->setCurrentItem(nullptr);
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < table->columnCount() && column < rows[row].size(); ++column) {
            const QString value=rows[row][column];
            auto *item=new QTableWidgetItem(value);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setToolTip(value);
            const QString header=table->horizontalHeaderItem(column)->text();
            if(header.contains(QStringLiteral("状态")) || header.contains(QStringLiteral("类型")) || header.contains(QStringLiteral("响应码"))) {
                const QString display=AdminVisuals::statusText(value);
                if(!display.isEmpty()) {
                    item->setData(Qt::AccessibleTextRole,display);
                    item->setData(Qt::UserRole+1,true);
                    item->setToolTip(display+QStringLiteral(" · ")+value);
                }
            }
            if(header.contains(QStringLiteral("营收")) || header.contains(QStringLiteral("余额")) ||
               header.contains(QStringLiteral("数量")) || header.contains(QStringLiteral("功率")) ||
               header.contains(QStringLiteral("次数")) || header.contains(QStringLiteral("时长")))
                item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
            table->setItem(row,column,item);
        }
        if(table->objectName()==QStringLiteral("stationManagementTable") && rows[row].size()>=5) {
            const QString details=QStringLiteral("%1\n%2\n纬度：%3\n经度：%4")
                .arg(rows[row][1],rows[row][2],rows[row][3],rows[row][4]);
            for(int column:{1,2}) {
                table->item(row,column)->setToolTip(details);
                table->item(row,column)->setWhatsThis(details);
                table->item(row,column)->setData(Qt::AccessibleDescriptionRole,details);
            }
        }
        if (!selectedKey.isEmpty() && !rows[row].isEmpty() && rows[row].first() == selectedKey) {
            table->selectRow(row);
        }
    }
    if(auto *empty=table->findChild<QLabel *>(QStringLiteral("tableEmptyState"))) empty->setVisible(rows.isEmpty());
}

int selectedId(QTableWidget *table)
{
    const QList<QTableWidgetSelectionRange> ranges = table->selectedRanges();
    if (ranges.isEmpty() || !table->item(ranges.first().topRow(), 0)) {
        return 0;
    }
    return table->item(ranges.first().topRow(), 0)->text().toInt();
}

bool confirmAction(QWidget *parent,const QString &title,const QString &description,const QString &action)
{
    QMessageBox dialog(QMessageBox::NoIcon,title,title,QMessageBox::Yes|QMessageBox::No,parent);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setInformativeText(description);
    dialog.setIconPixmap(AdminTheme::icon("alerts",QColor("#B94B43")).pixmap(36,36));
    dialog.setDefaultButton(QMessageBox::No);
    dialog.setEscapeButton(QMessageBox::No);
    dialog.button(QMessageBox::No)->setText(QStringLiteral("取消"));
    dialog.button(QMessageBox::Yes)->setText(action);
    dialog.button(QMessageBox::Yes)->setProperty("role","danger");
    dialog.setStyleSheet(QStringLiteral(
        "QMessageBox { background:#FFFFFF; }"
        "QLabel#qt_msgbox_label { font-size:18px; font-weight:600; }"
        "QLabel#qt_msgbox_informativelabel { color:#718078; min-width:340px; max-width:440px; padding:8px 0 16px; }"));
    return dialog.exec()==QMessageBox::Yes;
}

} // namespace

MainWindow::MainWindow(AppContext *context, const QString &adminToken, QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
    , m_adminToken(adminToken)
{
    AdminTheme::apply(*qApp);
    setWindowTitle(QStringLiteral("东软电动汽车充电桩应用管理平台 - 管理端"));
    resize(1360, 860);
    setMinimumSize(1120,680);

    auto *shell=new QWidget;
    auto *shellLayout=new QHBoxLayout(shell);
    shellLayout->setContentsMargins(0,0,0,0);
    shellLayout->setSpacing(0);
    auto *sidebar=new QFrame;
    sidebar->setProperty("role","sidebar");
    sidebar->setFixedWidth(208);
    auto *sideLayout=new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(18,28,18,22);
    sideLayout->setSpacing(6);
    auto *brand=textLabel(QStringLiteral("东软充电"),"sectionTitle");
    brand->setStyleSheet(QStringLiteral("font-size:24px; font-weight:700; color:#FFFFFF;"));
    sideLayout->addWidget(brand);
    sideLayout->addWidget(textLabel(QStringLiteral("运营管理平台")));
    sideLayout->addSpacing(22);
    const QStringList titles={QStringLiteral("运营总览"),QStringLiteral("电桩状态"),QStringLiteral("充电桩管理"),
        QStringLiteral("充电站管理"),QStringLiteral("用户管理"),QStringLiteral("接口服务"),QStringLiteral("请求日志"),QStringLiteral("系统健康")};
    const QStringList icons={"dashboard","chargers","bolt","stations","users","server","logs","health"};
    for(int i=0;i<titles.size();++i) {
        if(i==0 || i==2 || i==5) {
            if(i>0) sideLayout->addSpacing(12);
            auto *section=textLabel(i==0 ? QStringLiteral("运营概览") : i==2 ? QStringLiteral("资产管理") : QStringLiteral("系统运维"));
            section->setStyleSheet(QStringLiteral("font-size:11px; color:#8FAFA3; padding:6px 12px;"));
            sideLayout->addWidget(section);
        }
        auto *button=new QPushButton(titles.at(i));
        button->setObjectName(QString("adminNav%1").arg(i));
        button->setProperty("role","navButton");
        button->setIcon(AdminTheme::icon(icons.at(i),QColor("#BDD3C9")));
        button->setIconSize(QSize(19,19));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setMinimumHeight(40);
        button->setAccessibleName(titles.at(i));
        sideLayout->addWidget(button);
        m_navigation.append(button);
        connect(button,&QPushButton::clicked,this,[this,i] { m_tabs->setCurrentIndex(i); });
    }
    sideLayout->addStretch();
    sideLayout->addWidget(textLabel(QStringLiteral("ADMINISTRATOR")));
    auto *admin=textLabel(QStringLiteral("管理员 · admin"));
    admin->setStyleSheet(QStringLiteral("color:#FFFFFF; font-size:14px; padding:4px 0;"));
    sideLayout->addWidget(admin);
    sideLayout->addWidget(textLabel(QStringLiteral("本机演示环境  /  Qt Desktop")));
    shellLayout->addWidget(sidebar);

    auto *workspace=new QWidget;
    auto *workspaceLayout=new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(28,24,28,16);
    workspaceLayout->setSpacing(20);
    auto *heading=new QHBoxLayout;
    auto *headingText=new QVBoxLayout;
    headingText->setSpacing(5);
    m_pageTitle=textLabel(titles.first(),"pageTitle");
    m_pageSubtitle=textLabel(QString(),"pageSubtitle");
    headingText->addWidget(m_pageTitle);
    headingText->addWidget(m_pageSubtitle);
    heading->addLayout(headingText,1);
    auto *environment=textLabel(QStringLiteral("本机服务  ·  %1:%2").arg(m_context->host()).arg(m_context->port()),"statusGood");
    environment->setToolTip(QStringLiteral("当前管理窗口所属服务的监听地址"));
    heading->addWidget(environment);
    workspaceLayout->addLayout(heading);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName(QStringLiteral("adminTabs"));
    m_tabs->addTab(createDashboardPage(), QStringLiteral("销售业绩"));
    m_tabs->addTab(createPileStatusPage(), QStringLiteral("电桩状态"));
    m_tabs->addTab(createChargerManagementPage(), QStringLiteral("充电桩管理"));
    m_tabs->addTab(createStationManagementPage(), QStringLiteral("充电站管理"));
    m_tabs->addTab(createUserManagementPage(), QStringLiteral("用户管理"));
    m_tabs->addTab(createPlaceholderTablePage(
                     {QStringLiteral("项目"), QStringLiteral("值")},
                     {{QStringLiteral("监听地址"), QStringLiteral("%1:%2").arg(m_context->host()).arg(m_context->port())},
                      {QStringLiteral("数据库"), m_context->databasePath()}}),
                 QStringLiteral("接口服务"));
    m_tabs->addTab(createRequestLogPage(), QStringLiteral("请求日志"));
    m_tabs->addTab(createHealthPage(), QStringLiteral("系统健康"));

    m_tabs->tabBar()->hide();
    workspaceLayout->addWidget(m_tabs,1);
    shellLayout->addWidget(workspace,1);
    setCentralWidget(shell);
    const QStringList descriptions={QStringLiteral("充电业务的每一份进展，清晰可见"),QStringLiteral("查看设备分布，点击状态以筛选电桩"),
        QStringLiteral("统一查看充电设备，按站点筛选并处理故障"),QStringLiteral("管理站点资产与充电资源配置"),
        QStringLiteral("查询用户资料与账户状态，敏感操作需再次确认"),QStringLiteral("本机服务配置与运行数据位置"),
        QStringLiteral("追踪服务请求与响应结果，不显示会话凭据"),QStringLiteral("核心服务运行情况与演示环境维护")};
    connect(m_tabs, &QTabWidget::currentChanged, this, [this,titles,descriptions](int index) {
        for(int i=0;i<m_navigation.size();++i) m_navigation.at(i)->setChecked(i==index);
        m_pageTitle->setText(titles.value(index));
        m_pageSubtitle->setText(descriptions.value(index));
        refreshCurrentPage();
    });
    m_navigation.first()->setChecked(true);
    m_pageSubtitle->setText(descriptions.first());
    statusBar()->setSizeGripEnabled(true);
    statusBar()->addPermanentWidget(textLabel(QStringLiteral("演示业务数据  ·  当前页面每秒自动刷新")));
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshCurrentPage);
    m_refreshTimer->start();
}

QWidget *MainWindow::createDashboardPage()
{
    auto *page=new QWidget;
    auto *layout=pageLayout(page);
    auto *metrics=new QHBoxLayout;
    metrics->setSpacing(16);
    QList<QLabel *> values;
    const QStringList titles={QStringLiteral("今日营收"),QStringLiteral("本月营收"),QStringLiteral("累计营收")};
    const QStringList notes={QStringLiteral("今日已完成订单结算"),QStringLiteral("本月已完成订单结算"),QStringLiteral("平台全部已完成订单")};
    const QStringList names={"todayRevenueMetric","monthRevenueMetric","totalRevenueMetric"};
    for(int i=0;i<3;++i) {
        auto *card=panel();
        auto *cardLayout=qobject_cast<QVBoxLayout *>(card->layout());
        auto *top=new QHBoxLayout;
        top->addWidget(textLabel(titles.at(i)));
        top->addStretch();
        auto *icon=new QLabel;
        icon->setPixmap(AdminTheme::icon(i==2 ? "dashboard" : "orders",QColor("#00856A")).pixmap(22,22));
        top->addWidget(icon);
        cardLayout->addLayout(top);
        auto *value=textLabel(QStringLiteral("—"),"metricValue");
        value->setObjectName(names.at(i));
        value->setAccessibleName(titles.at(i));
        cardLayout->addWidget(value);
        cardLayout->addWidget(textLabel(notes.at(i)));
        cardLayout->setSpacing(5);
        values.append(value);
        metrics->addWidget(card,1);
    }
    layout->addLayout(metrics);
    auto *middle=new QHBoxLayout;
    middle->setSpacing(16);
    auto *chartCard=panel();
    auto *chartLayout=qobject_cast<QVBoxLayout *>(chartCard->layout());
    auto *chartHeading=new QHBoxLayout;
    chartHeading->addWidget(textLabel(QStringLiteral("营收趋势"),"sectionTitle"));
    chartHeading->addStretch();
    auto *rangeBox=new QComboBox;
    rangeBox->setObjectName(QStringLiteral("revenueRange"));
    rangeBox->setAccessibleName(QStringLiteral("营收统计时段"));
    rangeBox->addItem(QStringLiteral("最近 7 日"),7);
    rangeBox->addItem(QStringLiteral("最近 30 日"),30);
    rangeBox->setFixedWidth(138);
    chartHeading->addWidget(rangeBox);
    chartLayout->addLayout(chartHeading);
    auto *chart=new AdminVisuals::RevenueChart;
    chart->setObjectName(QStringLiteral("adminRevenueChart"));
    chartLayout->addWidget(chart,1);
    chartLayout->addWidget(textLabel(QStringLiteral("金额单位：元  ·  按已完成订单结算时间统计")));
    middle->addWidget(chartCard,3);

    auto *stateCard=panel(QStringLiteral("设备概况"));
    auto *stateLayout=qobject_cast<QVBoxLayout *>(stateCard->layout());
    stateLayout->setSpacing(8);
    auto *ring=new AdminVisuals::StateRing;
    ring->setMaximumHeight(160);
    stateLayout->addWidget(ring,1);
    const QStringList states={"idle","reserved","charging","fault","restarting"};
    QHash<QString,QLabel *> counts;
    auto *legend=new QGridLayout;
    legend->setHorizontalSpacing(18);
    legend->setVerticalSpacing(4);
    for(int i=0;i<states.size();++i) {
        const auto code=states.at(i);
        auto *row=new QWidget;
        auto *rowLayout=new QHBoxLayout(row);
        rowLayout->setContentsMargins(0,0,0,0);
        auto *caption=textLabel(AdminVisuals::statusText(code));
        caption->setStyleSheet(QString("color:%1; font-size:12px;").arg(AdminVisuals::statusColor(code).name()));
        rowLayout->addWidget(caption);
        rowLayout->addStretch();
        auto *number=textLabel("—");
        number->setStyleSheet(QStringLiteral("font-weight:600; color:#18352D; font-size:12px;"));
        rowLayout->addWidget(number);
        counts.insert(code,number);
        legend->addWidget(row,i/2,i%2);
    }
    stateLayout->addLayout(legend);
    middle->addWidget(stateCard,1);
    layout->addLayout(middle,3);

    auto *recordCard=panel(QStringLiteral("每日营收明细"));
    auto *records=qobject_cast<QVBoxLayout *>(recordCard->layout());
    auto *trend=makeTable({QStringLiteral("日期"),QStringLiteral("营收")});
    trend->setObjectName(QStringLiteral("revenueTrendTable"));
    trend->verticalHeader()->setDefaultSectionSize(36);
    records->addWidget(trend,1);
    layout->addWidget(recordCard,2);

    auto refresh=[this,rangeBox,values,trend,chart,ring,counts] {
        queryView(AdminView::Summary,{{"rangeDays",rangeBox->currentData().toInt()}},trend,[=](const QJsonObject &summary) {
            const auto revenue=summary.value("revenue").toObject();
            const QStringList keys={"todayRevenueFen","monthRevenueFen","totalRevenueFen"};
            for(int i=0;i<3;++i) values.at(i)->setText(moneyText(revenue.value(keys.at(i)).toInteger()));
            const auto points=summary.value("trend").toArray();
            chart->setPoints(points);
            const auto states=summary.value("statusCounts").toObject();
            ring->setCounts(states);
            for(auto it=counts.constBegin();it!=counts.constEnd();++it) it.value()->setText(QString::number(states.value(it.key()).toInt()));
            QList<QStringList> rows;
            for(const auto &value:points) {
                const auto point=value.toObject();
                rows.append({point.value("date").toString(),moneyText(point.value("revenueFen").toInteger())});
            }
            fillTable(trend,rows);
        });
    };
    connect(rangeBox,&QComboBox::currentIndexChanged,this,refresh);
    registerPageRefresh(page,refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createPileStatusPage()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    auto *summaryTable = makeTable({QStringLiteral("状态"), QStringLiteral("数量"), QStringLiteral("占比")});
    auto *detailTable = makeTable({QStringLiteral("桩ID"), QStringLiteral("编号"), QStringLiteral("所属电站"), QStringLiteral("类型"), QStringLiteral("功率(kW)"), QStringLiteral("状态"), QStringLiteral("累计次数"), QStringLiteral("累计时长(s)")});
    summaryTable->setObjectName(QStringLiteral("pileStatusSummaryTable"));
    detailTable->setObjectName(QStringLiteral("pileStatusDetailTable"));
    summaryTable->verticalHeader()->setDefaultSectionSize(32);
    auto *ring=new AdminVisuals::StateRing;
    auto *all=new QPushButton(QStringLiteral("显示全部电桩"));
    const QList<QPair<QString, QString>> states = {
        {QStringLiteral("idle"), QStringLiteral("空闲")},
        {QStringLiteral("reserved"), QStringLiteral("预约")},
        {QStringLiteral("charging"), QStringLiteral("充电")},
        {QStringLiteral("fault"), QStringLiteral("故障")},
        {QStringLiteral("restarting"), QStringLiteral("重启中")}
    };
    auto refresh = [this, summaryTable, detailTable, states, ring]() {
        queryView(AdminView::Summary, {}, summaryTable, [=](const QJsonObject &summary) {
            const QJsonObject status = summary.value(QStringLiteral("statusCounts")).toObject();
            ring->setCounts(status);
            QList<QStringList> rows;
            const int total = status.value(QStringLiteral("total")).toInt();
            for (const auto &state : states) {
                const int count = status.value(state.first).toInt();
                rows.append({state.second, QString::number(count),
                             total == 0 ? QStringLiteral("0%") : QStringLiteral("%1%").arg(qRound(count * 100.0 / total))});
            }
            fillTable(summaryTable, rows);
            const int selectedState = summaryTable->currentRow();
            const QString statusFilter = selectedState >= 0 && selectedState < states.size()
                ? states.at(selectedState).first : QString();
            queryRows(AdminView::Chargers, {{"status",statusFilter}}, detailTable);
        });
    };
    connect(summaryTable, &QTableWidget::cellClicked, this, [this, detailTable, states](int row, int) {
        if (row >= 0 && row < states.size()) {
            queryRows(AdminView::Chargers, {{"status",states.at(row).first}}, detailTable);
        }
    });
    connect(all,&QPushButton::clicked,this,[summaryTable,refresh] {
        summaryTable->clearSelection(); summaryTable->setCurrentItem(nullptr); refresh();
    });
    auto *top=new QHBoxLayout;
    top->setSpacing(16);
    auto *ringCard=panel(QStringLiteral("设备分布"));
    ringCard->setMaximumWidth(260);
    ringCard->layout()->addWidget(ring);
    auto *summaryCard=panel(QStringLiteral("状态统计"));
    summaryCard->layout()->addWidget(summaryTable);
    top->addWidget(ringCard,1);
    top->addWidget(summaryCard,3);
    layout->addLayout(top,2);
    auto *details=panel();
    auto *detailLayout=qobject_cast<QVBoxLayout *>(details->layout());
    auto *heading=new QHBoxLayout;
    heading->addWidget(textLabel(QStringLiteral("电桩运行明细"),"sectionTitle"));
    heading->addStretch(); heading->addWidget(all);
    detailLayout->addLayout(heading);
    detailLayout->addWidget(detailTable,1);
    layout->addWidget(details,3);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createChargerManagementPage()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    auto *toolbar = new QHBoxLayout;
    auto *stationBox = new QComboBox;
    stationBox->setObjectName(QStringLiteral("chargerStationFilter"));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"));
    auto *restartButton = new QPushButton(QStringLiteral("远程重启故障桩"));
    stationBox->setMinimumWidth(210);
    stationBox->setAccessibleName(QStringLiteral("按所属电站筛选"));
    refreshButton->setIcon(AdminTheme::icon("refresh"));
    restartButton->setProperty("role","danger");
    auto *table = makeTable({QStringLiteral("桩ID"), QStringLiteral("编号"), QStringLiteral("所属电站"), QStringLiteral("类型"), QStringLiteral("功率(kW)"), QStringLiteral("状态"), QStringLiteral("累计次数"), QStringLiteral("累计时长(s)")});
    table->setObjectName(QStringLiteral("chargerManagementTable"));
    auto refresh = [this, stationBox, table]() {
        queryView(AdminView::Stations, {}, stationBox, [=](const QJsonObject &data) {
            const int selectedStationId = stationBox->currentData().toInt();
            const QSignalBlocker blocker(stationBox);
            stationBox->clear();
            stationBox->addItem(QStringLiteral("全部电站"), 0);
            for (const auto &value : data.value("rows").toArray()) {
                const auto row = value.toArray();
                stationBox->addItem(row.at(1).toString(), row.at(0).toString().toInt());
            }
            const int restoredIndex = stationBox->findData(selectedStationId);
            stationBox->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);
            queryRows(AdminView::Chargers, {{"stationId",stationBox->currentData().toInt()}}, table);
        });
    };
    connect(stationBox, &QComboBox::currentIndexChanged, this, refresh);
    connect(refreshButton, &QPushButton::clicked, this, refresh);
    connect(restartButton, &QPushButton::clicked, this, [this, table, refresh, restartButton]() {
        const int chargerId = selectedId(table);
        if (chargerId <= 0) {
            QMessageBox::information(this, QStringLiteral("请选择电桩"), QStringLiteral("请先选中一个故障电桩。"));
            return;
        }
        if (!confirmAction(this,QStringLiteral("确认重启故障电桩"),
            QStringLiteral("即将远程重启电桩 #%1。操作提交后，设备会先进入重启状态，再恢复为空闲。请确认已选中正确的故障电桩。").arg(chargerId),QStringLiteral("确认重启"))) {
            return;
        }
        mutate("admin.charger_restart", {{"chargerId",chargerId}}, {restartButton}, [this,refresh] {
            refresh();
            QTimer::singleShot(1700,this,refresh);
        });
    });
    toolbar->addWidget(textLabel(QStringLiteral("所属电站")));
    toolbar->addWidget(stationBox);
    toolbar->addWidget(refreshButton);
    toolbar->addStretch();
    toolbar->addWidget(restartButton);
    auto *card=panel();
    auto *content=qobject_cast<QVBoxLayout *>(card->layout());
    content->addLayout(toolbar);
    content->addWidget(table,1);
    content->addWidget(textLabel(QStringLiteral("选中一行可查看设备信息；故障重启需确认后执行。")));
    layout->addWidget(card);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createStationManagementPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(16);
    auto *table = makeTable({QStringLiteral("站ID"), QStringLiteral("站名"), QStringLiteral("地址"), QStringLiteral("纬度"), QStringLiteral("经度"), QStringLiteral("总桩数"), QStringLiteral("可用比例"), QStringLiteral("可选预测配置")});
    table->setObjectName(QStringLiteral("stationManagementTable"));
    auto *formBox = new QGroupBox(QStringLiteral("新增充电站"));
    auto *form = new QFormLayout(formBox);
    auto *nameEdit = new QLineEdit;
    auto *addressEdit = new QLineEdit;
    auto *latSpin = new QDoubleSpinBox;
    auto *lonSpin = new QDoubleSpinBox;
    auto *priceSpin = new QDoubleSpinBox;
    auto *fastSpin = new QSpinBox;
    auto *slowSpin = new QSpinBox;
    auto *createButton = new QPushButton(QStringLiteral("新增站点"));
    createButton->setProperty("role","primary");
    createButton->setIcon(AdminTheme::icon("plus",Qt::white));
    nameEdit->setPlaceholderText(QStringLiteral("请输入电站名称"));
    addressEdit->setPlaceholderText(QStringLiteral("请输入详细地址"));
    nameEdit->setAccessibleName(QStringLiteral("电站名称"));
    addressEdit->setAccessibleName(QStringLiteral("电站地址"));
    form->setContentsMargins(16,24,16,18);
    form->setSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    latSpin->setRange(-90.0, 90.0);
    lonSpin->setRange(-180.0, 180.0);
    latSpin->setDecimals(6);
    lonSpin->setDecimals(6);
    priceSpin->setRange(0.01, 99.99);
    priceSpin->setDecimals(2);
    fastSpin->setRange(0, 100);
    slowSpin->setRange(0, 100);
    form->addRow(QStringLiteral("站名"), nameEdit);
    form->addRow(QStringLiteral("地址"), addressEdit);
    form->addRow(QStringLiteral("纬度"), latSpin);
    form->addRow(QStringLiteral("经度"), lonSpin);
    form->addRow(QStringLiteral("单价(元/度)"), priceSpin);
    form->addRow(QStringLiteral("快桩数"), fastSpin);
    form->addRow(QStringLiteral("慢桩数"), slowSpin);
    form->addRow(createButton);
    auto refresh = [this, table]() {
        queryRows(AdminView::Stations, {}, table);
    };
    connect(createButton, &QPushButton::clicked, this, [=]() {
        mutate("admin.station_create", QJsonObject{
            {QStringLiteral("name"), nameEdit->text()},
            {QStringLiteral("address"), addressEdit->text()},
            {QStringLiteral("latitude"), latSpin->value()},
            {QStringLiteral("longitude"), lonSpin->value()},
            {QStringLiteral("priceFenPerKwh"), qRound(priceSpin->value() * 100.0)},
            {QStringLiteral("fastChargerCount"), fastSpin->value()},
            {QStringLiteral("slowChargerCount"), slowSpin->value()}}, {createButton}, refresh);
    });
    // 坐标与已退出交付范围的预测配置仍保留在数据模型，不挤占主列表。
    table->setColumnHidden(3,true);
    table->setColumnHidden(4,true);
    table->setColumnHidden(7,true);
    table->horizontalHeader()->setSectionResizeMode(5,QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(6,QHeaderView::Fixed);
    table->setColumnWidth(5,70);
    table->setColumnWidth(6,90);
    auto *list=panel(QStringLiteral("站点资产"),QStringLiteral("悬停站名或地址可查看坐标。可用比例按非故障电桩计算，非网络在线率。"));
    list->layout()->addWidget(table);
    auto *scroll=new QScrollArea;
    scroll->setObjectName(QStringLiteral("stationFormScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(formBox);
    scroll->setMinimumWidth(306);
    scroll->setMaximumWidth(340);
    layout->addWidget(list,1);
    layout->addWidget(scroll);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createUserManagementPage()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    auto *toolbar = new QHBoxLayout;
    auto *searchEdit = new QLineEdit;
    auto *limitSpin = new QSpinBox;
    auto *offsetSpin = new QSpinBox;
    auto *refreshButton = new QPushButton(QStringLiteral("查询"));
    auto *freezeButton = new QPushButton(QStringLiteral("冻结"));
    auto *activeButton = new QPushButton(QStringLiteral("解冻"));
    searchEdit->setAccessibleName(QStringLiteral("按手机号搜索用户"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(180);
    searchEdit->setMaximumWidth(300);
    refreshButton->setProperty("role","primary");
    refreshButton->setIcon(AdminTheme::icon("search",Qt::white));
    freezeButton->setProperty("role","danger");
    limitSpin->setFixedWidth(92);
    offsetSpin->setFixedWidth(100);
    limitSpin->setAccessibleName(QStringLiteral("每页记录数"));
    offsetSpin->setAccessibleName(QStringLiteral("跳过记录数"));
    offsetSpin->setToolTip(QStringLiteral("从第几条记录之后开始查询；0 表示从第一条开始"));
    auto *table = makeTable({QStringLiteral("用户ID"), QStringLiteral("手机号"), QStringLiteral("昵称"), QStringLiteral("余额"), QStringLiteral("注册时间"), QStringLiteral("状态")});
    searchEdit->setObjectName(QStringLiteral("userSearchEdit"));
    limitSpin->setObjectName(QStringLiteral("userLimitSpin"));
    offsetSpin->setObjectName(QStringLiteral("userOffsetSpin"));
    table->setObjectName(QStringLiteral("userManagementTable"));
    searchEdit->setPlaceholderText(QStringLiteral("手机号模糊搜索"));
    limitSpin->setRange(1, 100);
    limitSpin->setValue(20);
    offsetSpin->setRange(0, 10000);
    auto refresh = [this, table, searchEdit, limitSpin, offsetSpin]() {
        queryRows(AdminView::Users, {{"mobileLike",searchEdit->text().trimmed()},{"limit",limitSpin->value()},{"offset",offsetSpin->value()}}, table);
    };
    auto setStatus = [this, table, refresh, freezeButton, activeButton](const QString &status) {
        const int userId = selectedId(table);
        if (userId <= 0) {
            QMessageBox::information(this, QStringLiteral("请选择用户"), QStringLiteral("请先选中一个用户。"));
            return;
        }
        const QString actionText = status == QStringLiteral("frozen") ? QStringLiteral("冻结") : QStringLiteral("解冻");
        if (!confirmAction(this,QStringLiteral("确认")+actionText+QStringLiteral("用户"),
            QStringLiteral("即将%1用户 #%2 的账户。此操作将写入业务数据库并记录操作日志，请核对当前选择。").arg(actionText).arg(userId),
            QStringLiteral("确认")+actionText)) {
            return;
        }
        mutate("admin.user_set_status", {{"userId",userId},{"status",status}}, {freezeButton,activeButton}, refresh);
    };
    connect(refreshButton, &QPushButton::clicked, this, refresh);
    connect(searchEdit,&QLineEdit::returnPressed,this,refresh);
    connect(freezeButton, &QPushButton::clicked, this, [setStatus]() { setStatus(QStringLiteral("frozen")); });
    connect(activeButton, &QPushButton::clicked, this, [setStatus]() { setStatus(QStringLiteral("active")); });
    toolbar->addWidget(searchEdit);
    toolbar->addWidget(new QLabel(QStringLiteral("每页")));
    toolbar->addWidget(limitSpin);
    toolbar->addWidget(new QLabel(QStringLiteral("跳过")));
    toolbar->addWidget(offsetSpin);
    toolbar->addWidget(refreshButton);
    toolbar->addStretch();
    toolbar->addWidget(freezeButton);
    toolbar->addWidget(activeButton);
    auto *card=panel();
    auto *content=qobject_cast<QVBoxLayout *>(card->layout());
    content->addLayout(toolbar);
    content->addWidget(table,1);
    content->addWidget(textLabel(QStringLiteral("账户操作作用于当前选中用户；冻结与解冻均需再次确认。")));
    layout->addWidget(card);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createRequestLogPage()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    auto *table = makeTable(
        {QStringLiteral("请求ID"), QStringLiteral("动作"), QStringLiteral("响应码"), QStringLiteral("时间")});
    table->setObjectName(QStringLiteral("requestLogTable"));
    auto refresh = [this, table]() {
        queryView(AdminView::RequestLog, {}, table, [table](const QJsonObject &data) {
            QList<QStringList> rows;
            {
                const QJsonArray items = data.value(QStringLiteral("items")).toArray();
                for (const QJsonValue &itemValue : items) {
                    const QJsonObject item = itemValue.toObject();
                    rows.append({
                        item.value(QStringLiteral("requestId")).toString(),
                        item.value(QStringLiteral("action")).toString(),
                        item.value(QStringLiteral("code")).toString(),
                        item.value(QStringLiteral("createdAt")).toString()
                    });
                }
            }
            fillTable(table, rows);
        });
    };
    auto *card=panel(QStringLiteral("最近服务请求"),QStringLiteral("展示请求标识、动作与响应摘要；长内容可悬停查看。"));
    card->layout()->addWidget(table);
    layout->addWidget(card);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createHealthPage()
{
    auto *page = new QWidget;
    auto *layout = pageLayout(page);
    auto *reset = new QPushButton(QStringLiteral("恢复演示黄金数据"));
    reset->setObjectName(QStringLiteral("demoResetButton"));
    reset->setProperty("role","danger");
    connect(reset,&QPushButton::clicked,this,[this,reset] {
        if (!confirmAction(this,QStringLiteral("确认复位演示数据"),
            QStringLiteral("此操作将丢失当前演示业务状态（用户变更、订单、设备事件及旧请求日志），并恢复批准的黄金数据。请仅在重新准备演示时执行。"),
            QStringLiteral("恢复黄金数据"))) return;
        if (reset->property("requestId").toString().isEmpty())
            reset->setProperty("requestId",QUuid::createUuid().toString(QUuid::WithoutBraces));
        reset->setEnabled(false);
        m_context->executeLocal({1,reset->property("requestId").toString(),"demo.reset",m_adminToken,{{"confirmation","RESET_DEMO"}}},
            this,[this,reset](const QByteArray &bytes) {
                reset->setEnabled(true);
                const auto response=ev::protocol::parseResponse(bytes);
                if (!response.ok) {
                    QMessageBox::warning(this,QStringLiteral("复位未完成"),response.code+"："+response.message+
                        QStringLiteral("\n再次确认将使用同一请求继续，已提交的复位不会重复执行。"));
                    return;
                }
                reset->setProperty("requestId",QString{});
                statusBar()->showMessage(response.message,15000);
                refreshCurrentPage();
            });
    });
    auto *table = makeTable({QStringLiteral("检查项"), QStringLiteral("状态"), QStringLiteral("说明")});
    table->setObjectName(QStringLiteral("healthTable"));
    auto refresh = [this, table]() {
        const QJsonObject health = m_context->healthSnapshot();
        const QString forecastState = health.value(QStringLiteral("forecastRunId")).isNull()
            ? QStringLiteral("无活动预测批次")
            : QStringLiteral("活动批次：") + health.value(QStringLiteral("forecastRunId")).toString();
        fillTable(table,
                  {{QStringLiteral("服务监听"), QStringLiteral("正常"), QStringLiteral("%1:%2").arg(m_context->host()).arg(m_context->port())},
                   {QStringLiteral("数据库 schema"), QStringLiteral("正常"), QString::number(health.value(QStringLiteral("schemaVersion")).toInt())},
                   {QStringLiteral("快照版本"), QStringLiteral("正常"), QString::number(health.value(QStringLiteral("snapshotVersion")).toInt())},
                   {QStringLiteral("扩展预测"), QStringLiteral("可选模块"), QStringLiteral("不参与核心验收 · ")+forecastState},
                   {QStringLiteral("模拟器状态"), QStringLiteral("独立监测"), QStringLiteral("请查看模拟器面板；本页尚未订阅其心跳")}});
    };
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Fixed);
    table->setColumnWidth(0,160);
    table->setColumnWidth(1,140);
    auto *health=panel(QStringLiteral("核心服务检查"),QStringLiteral("服务与数据状态按本机运行上下文展示，可选扩展不影响核心演示。"));
    health->layout()->addWidget(table);
    layout->addWidget(health,1);
    auto *danger=panel(QStringLiteral("演示数据维护"),QStringLiteral("恢复黄金数据会清除本轮用户变更、订单、设备事件和旧请求日志。请仅在重新准备演示时操作。"));
    danger->setObjectName(QStringLiteral("adminDangerZone"));
    danger->setStyleSheet(QStringLiteral("QFrame#adminDangerZone { border:1px solid #E8C9C4; background:#FFFDFC; border-radius:14px; }"));
    auto *actions=new QHBoxLayout;
    actions->addWidget(textLabel(QStringLiteral("此操作需二次确认")));
    actions->addStretch();
    actions->addWidget(reset);
    qobject_cast<QVBoxLayout *>(danger->layout())->addLayout(actions);
    layout->addWidget(danger);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createPlaceholderTablePage(const QStringList &headers, const QList<QStringList> &rows)
{
    Q_UNUSED(headers)
    auto *page=new QWidget;
    auto *layout=pageLayout(page);
    auto *intro=panel(QStringLiteral("运行配置"),QStringLiteral("当前管理窗口与服务端共用以下运行配置；数据库路径可选中或复制。"));
    auto *content=qobject_cast<QVBoxLayout *>(intro->layout());
    for(const auto &row:rows) {
        if(row.size()<2) continue;
        content->addSpacing(12);
        content->addWidget(textLabel(row.first()));
        auto *line=new QHBoxLayout;
        auto *value=new QLabel(row.at(1));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
        value->setFocusPolicy(Qt::StrongFocus);
        value->setAccessibleName(row.first());
        value->setStyleSheet(QStringLiteral("color:#18352D; font-size:15px; padding:12px; background:#F5F6F2; border-radius:8px;"));
        line->addWidget(value,1);
        auto *copy=new QPushButton(QStringLiteral("复制"));
        copy->setAccessibleName(QStringLiteral("复制")+row.first());
        connect(copy,&QPushButton::clicked,this,[this,value] {
            QApplication::clipboard()->setText(value->text());
            statusBar()->showMessage(QStringLiteral("已复制到剪贴板"),3000);
        });
        line->addWidget(copy);
        content->addLayout(line);
    }
    layout->addWidget(intro);
    auto *notes=panel(QStringLiteral("三端协作"),QStringLiteral("Qt 管理端负责业务处理与数据持久化，用户端和设备模拟器通过 JSON/TCP 连接本机服务。"));
    notes->layout()->addWidget(textLabel(QStringLiteral("数据存储：SQLite 运行副本     连接方式：TCP     运行模式：桌面管理端")));
    layout->addWidget(notes);
    layout->addStretch();
    return page;
}

void MainWindow::registerPageRefresh(QWidget *page, std::function<void()> refresh)
{
    m_pageRefreshers.insert(page, std::move(refresh));
}

void MainWindow::refreshCurrentPage()
{
    if (!m_tabs) {
        return;
    }
    const auto refresh = m_pageRefreshers.constFind(m_tabs->currentWidget());
    if (refresh != m_pageRefreshers.constEnd()) {
        refresh.value()();
    }
}
void MainWindow::queryView(AdminView view, const QJsonObject &parameters, QObject *receiver,
                           std::function<void(QJsonObject)> callback)
{
    const auto revision = receiver->property("refreshRevision").toULongLong() + 1;
    receiver->setProperty("refreshRevision",revision);
    m_context->queryAdmin(view,m_adminToken,parameters,receiver,[this,receiver,revision,callback](const QByteArray &bytes) {
        if (receiver->property("refreshRevision").toULongLong() != revision) return;
        const auto response = ev::protocol::parseResponse(bytes);
        if (!response.ok) { statusBar()->showMessage(response.code + "：" + response.message,5000); return; }
        callback(response.data.toObject());
    });
}
void MainWindow::queryRows(AdminView view, const QJsonObject &parameters, QTableWidget *table)
{
    queryView(view,parameters,table,[table](const QJsonObject &data) {
        QList<QStringList> rows;
        for (const auto &value : data.value("rows").toArray()) {
            QStringList row;
            for (const auto &cell : value.toArray()) row.append(cell.toString());
            rows.append(row);
        }
        fillTable(table,rows);
    });
}
void MainWindow::mutate(const QString &action, const QJsonObject &payload, const QList<QWidget *> &controls,
                        std::function<void()> callback)
{
    for (auto *control : controls) if (!control->isEnabled()) return;
    for (auto *control : controls) control->setEnabled(false);
    m_context->executeLocal({1,QUuid::createUuid().toString(QUuid::WithoutBraces),action,m_adminToken,payload},
        this,[this,controls,callback](const QByteArray &bytes) {
            for (auto *control : controls) control->setEnabled(true);
            const auto response = ev::protocol::parseResponse(bytes);
            if (!response.ok) { QMessageBox::warning(this,QStringLiteral("操作失败"),response.code + "：" + response.message); return; }
            callback();
        });
}
