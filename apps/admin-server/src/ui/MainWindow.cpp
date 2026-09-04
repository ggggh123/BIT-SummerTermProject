#include "ui/MainWindow.h"

#include <QHeaderView>
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
                     {{QStringLiteral("监听地址"), QStringLiteral("127.0.0.1:4545")},
                      {QStringLiteral("数据库"), m_context->databasePath()}}),
                 QStringLiteral("接口服务"));

    setCentralWidget(tabs);
}

QWidget *MainWindow::createDashboardPage()
{
    const QJsonObject summary = m_context->dashboardService()->summary();
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    layout->addWidget(metricLabel(QStringLiteral("今日营收"), QString::number(summary.value(QStringLiteral("todayRevenue")).toDouble(), 'f', 2)));
    layout->addWidget(metricLabel(QStringLiteral("本月营收"), QString::number(summary.value(QStringLiteral("monthRevenue")).toDouble(), 'f', 2)));
    layout->addWidget(metricLabel(QStringLiteral("总营收"), QString::number(summary.value(QStringLiteral("totalRevenue")).toDouble(), 'f', 2)));
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createPileStatusPage()
{
    const QJsonObject summary = m_context->dashboardService()->summary();
    return createPlaceholderTablePage(
        {QStringLiteral("状态"), QStringLiteral("数量")},
        {{QStringLiteral("闲置"), QString::number(summary.value(QStringLiteral("pileIdle")).toInt())},
         {QStringLiteral("在用"), QString::number(summary.value(QStringLiteral("pileUsing")).toInt())},
         {QStringLiteral("故障"), QString::number(summary.value(QStringLiteral("pileFault")).toInt())},
         {QStringLiteral("总数"), QString::number(summary.value(QStringLiteral("pileTotal")).toInt())}});
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

