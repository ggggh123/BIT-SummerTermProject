#include "ui/MainWindow.h"

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

#ifdef EV_HAS_QT_CHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#endif

namespace {

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
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
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
            table->setItem(row, column, new QTableWidgetItem(rows[row][column]));
        }
        if (!selectedKey.isEmpty() && !rows[row].isEmpty() && rows[row].first() == selectedKey) {
            table->selectRow(row);
        }
    }
}

int selectedId(QTableWidget *table)
{
    const QList<QTableWidgetSelectionRange> ranges = table->selectedRanges();
    if (ranges.isEmpty() || !table->item(ranges.first().topRow(), 0)) {
        return 0;
    }
    return table->item(ranges.first().topRow(), 0)->text().toInt();
}

} // namespace

MainWindow::MainWindow(AppContext *context, QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
{
    setWindowTitle(QStringLiteral("东软电动汽车充电桩应用管理平台 - 管理端"));
    resize(1180, 720);

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

    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this]() {
        refreshCurrentPage();
    });
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshCurrentPage);
    m_refreshTimer->start();
}

QWidget *MainWindow::createDashboardPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *rangeBox = new QComboBox;
    rangeBox->addItem(QStringLiteral("最近 7 日"), 7);
    rangeBox->addItem(QStringLiteral("最近 30 日"), 30);
    auto *today = metricLabel(QStringLiteral("今日营收"), QString());
    auto *month = metricLabel(QStringLiteral("本月营收"), QString());
    auto *total = metricLabel(QStringLiteral("总营收"), QString());
    auto *trend = makeTable({QStringLiteral("日期"), QStringLiteral("营收")});
    today->setObjectName(QStringLiteral("todayRevenueMetric"));
    month->setObjectName(QStringLiteral("monthRevenueMetric"));
    total->setObjectName(QStringLiteral("totalRevenueMetric"));
    trend->setObjectName(QStringLiteral("revenueTrendTable"));
#ifdef EV_HAS_QT_CHARTS
    auto *series = new QLineSeries;
    auto *chart = new QChart;
    chart->addSeries(series);
    chart->legend()->hide();
    chart->setTitle(QStringLiteral("营收趋势"));
    auto *axisX = new QValueAxis;
    auto *axisY = new QValueAxis;
    axisX->setLabelFormat(QStringLiteral("%.0f"));
    axisX->setTitleText(QStringLiteral("天序号"));
    axisY->setTitleText(QStringLiteral("营收(元)"));
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    auto *chartView = new QChartView(chart);
    chartView->setMinimumHeight(220);
#endif

    auto refresh = [this, rangeBox, today, month, total, trend
#ifdef EV_HAS_QT_CHARTS
                    , series, axisX, axisY
#endif
                   ]() {
        const QJsonObject summary = m_context->dashboardService()->summary(rangeBox->currentData().toInt());
        const QJsonObject revenue = summary.value(QStringLiteral("revenue")).toObject();
        today->setText(QStringLiteral("今日营收：") + moneyText(static_cast<qint64>(revenue.value(QStringLiteral("todayRevenueFen")).toDouble())));
        month->setText(QStringLiteral("本月营收：") + moneyText(static_cast<qint64>(revenue.value(QStringLiteral("monthRevenueFen")).toDouble())));
        total->setText(QStringLiteral("总营收：") + moneyText(static_cast<qint64>(revenue.value(QStringLiteral("totalRevenueFen")).toDouble())));
        QList<QStringList> rows;
        const QJsonArray points = summary.value(QStringLiteral("trend")).toArray();
        double maxRevenueYuan = 0.0;
#ifdef EV_HAS_QT_CHARTS
        series->clear();
#endif
        int dayIndex = 1;
        for (const QJsonValue &pointValue : points) {
            const QJsonObject point = pointValue.toObject();
            const qint64 revenueFen = static_cast<qint64>(point.value(QStringLiteral("revenueFen")).toDouble());
            const double revenueYuan = revenueFen / 100.0;
            maxRevenueYuan = qMax(maxRevenueYuan, revenueYuan);
            rows.append({point.value(QStringLiteral("date")).toString(), moneyText(revenueFen)});
#ifdef EV_HAS_QT_CHARTS
            series->append(dayIndex, revenueYuan);
#endif
            ++dayIndex;
        }
#ifdef EV_HAS_QT_CHARTS
        axisX->setRange(1, qMax(1, points.size()));
        axisY->setRange(0, qMax(1.0, maxRevenueYuan));
#endif
        fillTable(trend, rows);
    };

    connect(rangeBox, &QComboBox::currentIndexChanged, this, refresh);
    layout->addWidget(rangeBox);
    layout->addWidget(today);
    layout->addWidget(month);
    layout->addWidget(total);
#ifdef EV_HAS_QT_CHARTS
    layout->addWidget(chartView);
#endif
    layout->addWidget(trend);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createPileStatusPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *summaryTable = makeTable({QStringLiteral("状态"), QStringLiteral("数量"), QStringLiteral("占比")});
    auto *detailTable = makeTable({QStringLiteral("桩ID"), QStringLiteral("编号"), QStringLiteral("所属电站"), QStringLiteral("类型"), QStringLiteral("功率(kW)"), QStringLiteral("状态"), QStringLiteral("累计次数"), QStringLiteral("累计时长(s)")});
    summaryTable->setObjectName(QStringLiteral("pileStatusSummaryTable"));
    detailTable->setObjectName(QStringLiteral("pileStatusDetailTable"));
    const QList<QPair<QString, QString>> states = {
        {QStringLiteral("idle"), QStringLiteral("空闲")},
        {QStringLiteral("reserved"), QStringLiteral("预约")},
        {QStringLiteral("charging"), QStringLiteral("充电")},
        {QStringLiteral("fault"), QStringLiteral("故障")},
        {QStringLiteral("restarting"), QStringLiteral("重启中")}
    };
    auto refresh = [this, summaryTable, detailTable, states]() {
        const QJsonObject summary = m_context->dashboardService()->summary();
        const QJsonObject status = summary.value(QStringLiteral("statusCounts")).toObject();
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
        fillTable(detailTable, m_context->dashboardService()->chargerRows(0, statusFilter));
    };
    connect(summaryTable, &QTableWidget::cellClicked, this, [this, detailTable, states](int row, int) {
        if (row >= 0 && row < states.size()) {
            fillTable(detailTable, m_context->dashboardService()->chargerRows(0, states.at(row).first));
        }
    });
    layout->addWidget(summaryTable);
    layout->addWidget(detailTable);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createChargerManagementPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *toolbar = new QHBoxLayout;
    auto *stationBox = new QComboBox;
    stationBox->setObjectName(QStringLiteral("chargerStationFilter"));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"));
    auto *restartButton = new QPushButton(QStringLiteral("远程重启故障桩"));
    auto *table = makeTable({QStringLiteral("桩ID"), QStringLiteral("编号"), QStringLiteral("所属电站"), QStringLiteral("类型"), QStringLiteral("功率(kW)"), QStringLiteral("状态"), QStringLiteral("累计次数"), QStringLiteral("累计时长(s)")});
    table->setObjectName(QStringLiteral("chargerManagementTable"));
    auto refresh = [this, stationBox, table]() {
        const int selectedStationId = stationBox->currentData().toInt();
        const QSignalBlocker blocker(stationBox);
        stationBox->clear();
        stationBox->addItem(QStringLiteral("全部电站"), 0);
        for (const QStringList &row : m_context->dashboardService()->stationRows()) {
            stationBox->addItem(row.value(1), row.value(0).toInt());
        }
        const int restoredIndex = stationBox->findData(selectedStationId);
        stationBox->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);
        fillTable(table, m_context->dashboardService()->chargerRows(stationBox->currentData().toInt()));
    };
    connect(stationBox, &QComboBox::currentIndexChanged, this, refresh);
    connect(refreshButton, &QPushButton::clicked, this, refresh);
    connect(restartButton, &QPushButton::clicked, this, [this, table, refresh]() {
        const int chargerId = selectedId(table);
        if (chargerId <= 0) {
            QMessageBox::information(this, QStringLiteral("请选择电桩"), QStringLiteral("请先选中一个故障电桩。"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("确认重启"), QStringLiteral("确认远程重启选中的故障电桩？")) != QMessageBox::Yes) {
            return;
        }
        QJsonObject data;
        const Result result = m_context->adminService()->chargerRestart(QJsonObject{{QStringLiteral("chargerId"), chargerId}}, &data);
        if (!result.ok) {
            QMessageBox::warning(this, QStringLiteral("重启失败"), result.message);
            return;
        }
        refresh();
        QTimer::singleShot(1700, this, refresh);
    });
    toolbar->addWidget(stationBox);
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(restartButton);
    layout->addLayout(toolbar);
    layout->addWidget(table);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createStationManagementPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *table = makeTable({QStringLiteral("站ID"), QStringLiteral("站名"), QStringLiteral("地址"), QStringLiteral("纬度"), QStringLiteral("经度"), QStringLiteral("总桩数"), QStringLiteral("在线率"), QStringLiteral("forecastEnabled")});
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
        fillTable(table, m_context->dashboardService()->stationRows());
    };
    connect(createButton, &QPushButton::clicked, this, [=]() {
        QJsonObject data;
        const Result result = m_context->adminService()->stationCreate(QJsonObject{
            {QStringLiteral("name"), nameEdit->text()},
            {QStringLiteral("address"), addressEdit->text()},
            {QStringLiteral("latitude"), latSpin->value()},
            {QStringLiteral("longitude"), lonSpin->value()},
            {QStringLiteral("priceFenPerKwh"), qRound(priceSpin->value() * 100.0)},
            {QStringLiteral("fastChargerCount"), fastSpin->value()},
            {QStringLiteral("slowChargerCount"), slowSpin->value()}}, &data);
        if (!result.ok) {
            QMessageBox::warning(this, QStringLiteral("新增失败"), result.message);
            return;
        }
        refresh();
    });
    layout->addWidget(table);
    layout->addWidget(formBox);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createUserManagementPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *toolbar = new QHBoxLayout;
    auto *searchEdit = new QLineEdit;
    auto *limitSpin = new QSpinBox;
    auto *offsetSpin = new QSpinBox;
    auto *refreshButton = new QPushButton(QStringLiteral("查询"));
    auto *freezeButton = new QPushButton(QStringLiteral("冻结"));
    auto *activeButton = new QPushButton(QStringLiteral("解冻"));
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
        fillTable(table, m_context->dashboardService()->userRows(searchEdit->text().trimmed(), limitSpin->value(), offsetSpin->value()));
    };
    auto setStatus = [this, table, refresh](const QString &status) {
        const int userId = selectedId(table);
        if (userId <= 0) {
            QMessageBox::information(this, QStringLiteral("请选择用户"), QStringLiteral("请先选中一个用户。"));
            return;
        }
        const QString actionText = status == QStringLiteral("frozen") ? QStringLiteral("冻结") : QStringLiteral("解冻");
        if (QMessageBox::question(this, QStringLiteral("确认") + actionText, QStringLiteral("确认") + actionText + QStringLiteral("选中的用户？")) != QMessageBox::Yes) {
            return;
        }
        QJsonObject data;
        const Result result = m_context->adminService()->userSetStatus(QJsonObject{{QStringLiteral("userId"), userId}, {QStringLiteral("status"), status}}, &data);
        if (!result.ok) {
            QMessageBox::warning(this, actionText + QStringLiteral("失败"), result.message);
            return;
        }
        refresh();
    };
    connect(refreshButton, &QPushButton::clicked, this, refresh);
    connect(freezeButton, &QPushButton::clicked, this, [setStatus]() { setStatus(QStringLiteral("frozen")); });
    connect(activeButton, &QPushButton::clicked, this, [setStatus]() { setStatus(QStringLiteral("active")); });
    toolbar->addWidget(searchEdit);
    toolbar->addWidget(new QLabel(QStringLiteral("每页")));
    toolbar->addWidget(limitSpin);
    toolbar->addWidget(new QLabel(QStringLiteral("偏移")));
    toolbar->addWidget(offsetSpin);
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(freezeButton);
    toolbar->addWidget(activeButton);
    layout->addLayout(toolbar);
    layout->addWidget(table);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createRequestLogPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *table = makeTable(
        {QStringLiteral("请求ID"), QStringLiteral("动作"), QStringLiteral("响应码"), QStringLiteral("时间")});
    table->setObjectName(QStringLiteral("requestLogTable"));
    auto refresh = [this, table]() {
        QJsonObject data;
        const Result result = m_context->requestLogService()->list(QString(), 50, 0, &data);
        QList<QStringList> rows;
        if (result.ok) {
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
        } else {
            rows.append({QStringLiteral("-"), QStringLiteral("-"), result.code, result.message});
        }
        fillTable(table, rows);
    };
    layout->addWidget(table);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createHealthPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *table = makeTable({QStringLiteral("检查项"), QStringLiteral("状态"), QStringLiteral("说明")});
    table->setObjectName(QStringLiteral("healthTable"));
    auto refresh = [this, table]() {
        const QJsonObject health = m_context->forecastService()->healthState();
        const QString forecastState = health.value(QStringLiteral("forecastRunId")).isNull()
            ? QStringLiteral("无活动预测批次")
            : QStringLiteral("活动批次：") + health.value(QStringLiteral("forecastRunId")).toString();
        fillTable(table,
                  {{QStringLiteral("服务监听"), QStringLiteral("正常"), QStringLiteral("%1:%2").arg(m_context->host()).arg(m_context->port())},
                   {QStringLiteral("数据库 schema"), QStringLiteral("正常"), QString::number(health.value(QStringLiteral("schemaVersion")).toInt())},
                   {QStringLiteral("快照版本"), QStringLiteral("正常"), QString::number(health.value(QStringLiteral("snapshotVersion")).toInt())},
                   {QStringLiteral("预测批次"), health.value(QStringLiteral("status")).toString() == QStringLiteral("ready") ? QStringLiteral("正常") : QStringLiteral("降级"), forecastState},
                   {QStringLiteral("模拟器心跳"), QStringLiteral("未接入"), QStringLiteral("当前服务端尚无 simulator.status 持久化来源")}});
    };
    layout->addWidget(table);
    registerPageRefresh(page, refresh);
    refresh();
    return page;
}

QWidget *MainWindow::createPlaceholderTablePage(const QStringList &headers, const QList<QStringList> &rows)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *table = new QTableWidget(rows.size(), headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);

    for (int row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < rows[row].size(); ++column) {
            table->setItem(row, column, new QTableWidgetItem(rows[row][column]));
        }
    }

    layout->addWidget(table);
    return page;
}

QLabel *MainWindow::metricLabel(const QString &title, const QString &value)
{
    auto *label = new QLabel(title + QStringLiteral("：") + value + QStringLiteral(" 元"));
    QFont font = label->font();
    font.setPointSize(14);
    font.setBold(true);
    label->setFont(font);
    label->setMinimumHeight(48);
    return label;
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
