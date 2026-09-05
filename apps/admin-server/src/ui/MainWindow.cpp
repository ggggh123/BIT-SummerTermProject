#include "ui/MainWindow.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

MainWindow::MainWindow(AppContext *context, QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
{
    setWindowTitle(QStringLiteral("东软电动汽车充电桩应用管理平台 - 管理端"));
    resize(1180, 720);

    auto *tabs = new QTabWidget;
    tabs->addTab(createDashboardPage(), QStringLiteral("销售业绩"));
    tabs->addTab(createPileStatusPage(), QStringLiteral("电桩状态"));
    tabs->addTab(createPlaceholderTablePage(
                     {QStringLiteral("电桩编号"), QStringLiteral("所属电站"), QStringLiteral("类型"), QStringLiteral("功率"), QStringLiteral("状态")},
                     m_context->dashboardService()->chargerRows()),
                 QStringLiteral("充电桩管理"));
    tabs->addTab(createPlaceholderTablePage(
                     {QStringLiteral("电站ID"), QStringLiteral("站名"), QStringLiteral("地址"), QStringLiteral("在线率")},
                     m_context->dashboardService()->stationRows()),
                 QStringLiteral("充电站管理"));
    tabs->addTab(createPlaceholderTablePage(
                     {QStringLiteral("用户ID"), QStringLiteral("手机号"), QStringLiteral("昵称"), QStringLiteral("余额"), QStringLiteral("状态")},
                     m_context->dashboardService()->userRows()),
                 QStringLiteral("用户管理"));
    tabs->addTab(createPlaceholderTablePage(
                     {QStringLiteral("项目"), QStringLiteral("值")},
                     {{QStringLiteral("监听地址"), QStringLiteral("%1:%2").arg(m_context->host()).arg(m_context->port())},
                      {QStringLiteral("数据库"), m_context->databasePath()}}),
                 QStringLiteral("接口服务"));
    tabs->addTab(createRequestLogPage(), QStringLiteral("请求日志"));
    tabs->addTab(createHealthPage(), QStringLiteral("系统健康"));

    setCentralWidget(tabs);
}

QWidget *MainWindow::createDashboardPage()
{
    const QJsonObject summary = m_context->dashboardService()->summary();
    const QJsonObject revenue = summary.value(QStringLiteral("revenue")).toObject();
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    layout->addWidget(metricLabel(QStringLiteral("今日营收"), QString::number(revenue.value(QStringLiteral("todayRevenueFen")).toDouble() / 100.0, 'f', 2)));
    layout->addWidget(metricLabel(QStringLiteral("本月营收"), QString::number(revenue.value(QStringLiteral("monthRevenueFen")).toDouble() / 100.0, 'f', 2)));
    layout->addWidget(metricLabel(QStringLiteral("总营收"), QString::number(revenue.value(QStringLiteral("totalRevenueFen")).toDouble() / 100.0, 'f', 2)));
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createPileStatusPage()
{
    const QJsonObject summary = m_context->dashboardService()->summary();
    const QJsonObject status = summary.value(QStringLiteral("statusCounts")).toObject();
    return createPlaceholderTablePage(
        {QStringLiteral("状态"), QStringLiteral("数量")},
        {{QStringLiteral("闲置"), QString::number(status.value(QStringLiteral("idle")).toInt())},
         {QStringLiteral("预约"), QString::number(status.value(QStringLiteral("reserved")).toInt())},
         {QStringLiteral("充电"), QString::number(status.value(QStringLiteral("charging")).toInt())},
         {QStringLiteral("故障"), QString::number(status.value(QStringLiteral("fault")).toInt())},
         {QStringLiteral("重启中"), QString::number(status.value(QStringLiteral("restarting")).toInt())},
         {QStringLiteral("总数"), QString::number(status.value(QStringLiteral("total")).toInt())}});
}

QWidget *MainWindow::createRequestLogPage()
{
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

    return createPlaceholderTablePage(
        {QStringLiteral("请求ID"), QStringLiteral("动作"), QStringLiteral("响应码"), QStringLiteral("时间")},
        rows);
}

QWidget *MainWindow::createHealthPage()
{
    const QJsonObject health = m_context->forecastService()->healthState();
    const QString forecastState = health.value(QStringLiteral("forecastRunId")).isNull()
        ? QStringLiteral("无活动预测批次")
        : QStringLiteral("活动批次：") + health.value(QStringLiteral("forecastRunId")).toString();

    return createPlaceholderTablePage(
        {QStringLiteral("检查项"), QStringLiteral("状态"), QStringLiteral("说明")},
        {{QStringLiteral("服务监听"), QStringLiteral("正常"), QStringLiteral("%1:%2").arg(m_context->host()).arg(m_context->port())},
         {QStringLiteral("数据库 schema"), QStringLiteral("正常"), QString::number(health.value(QStringLiteral("schemaVersion")).toInt())},
         {QStringLiteral("快照版本"), QStringLiteral("正常"), QString::number(health.value(QStringLiteral("snapshotVersion")).toInt())},
         {QStringLiteral("预测批次"), health.value(QStringLiteral("status")).toString() == QStringLiteral("ready") ? QStringLiteral("正常") : QStringLiteral("降级"), forecastState},
         {QStringLiteral("模拟器心跳"), QStringLiteral("未接入"), QStringLiteral("当前服务端尚无 simulator.status 持久化来源")}});
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

