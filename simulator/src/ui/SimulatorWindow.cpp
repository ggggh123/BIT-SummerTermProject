#include "ui/SimulatorWindow.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace ev::simulator {

SimulatorWindow::SimulatorWindow(ISimulatorClient *client, TelemetryEngine *engine,
                                 QWidget *parent)
    : QWidget(parent), client_(client), engine_(engine)
{
    setWindowTitle(QStringLiteral("EV 充电桩设备模拟器"));

    badge_ = new QLabel(QStringLiteral("未连接"), this);
    timeLabel_ = new QLabel(QStringLiteral("模拟时间：--"), this);
    eventLabel_ = new QLabel(QStringLiteral("事件总数：0"), this);

    runButton_ = new QPushButton(QStringLiteral("Run"), this);
    faultButton_ = new QPushButton(QStringLiteral("故障注入"), this);
    recoverButton_ = new QPushButton(QStringLiteral("故障恢复"), this);
    refreshButton_ = new QPushButton(QStringLiteral("刷新状态"), this);
    resetButton_ = new QPushButton(QStringLiteral("准备复位"), this);
    faultButton_->setEnabled(false);
    recoverButton_->setEnabled(false);

    table_ = new QTableWidget(0, 3, this);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("桩编号"), QStringLiteral("状态"), QStringLiteral("功率(kW)")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    logList_ = new QListWidget(this);

    auto *top = new QHBoxLayout;
    top->addWidget(badge_);
    top->addStretch();
    top->addWidget(timeLabel_);
    top->addSpacing(16);
    top->addWidget(eventLabel_);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(runButton_);
    buttons->addWidget(faultButton_);
    buttons->addWidget(recoverButton_);
    buttons->addWidget(refreshButton_);
    buttons->addWidget(resetButton_);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(buttons);
    layout->addWidget(table_, 1);
    layout->addWidget(new QLabel(QStringLiteral("事件日志"), this));
    layout->addWidget(logList_, 1);

    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(engine_->intervalMs());
    connect(tickTimer_, &QTimer::timeout, this, &SimulatorWindow::doTick);

    connect(runButton_, &QPushButton::clicked, this, &SimulatorWindow::toggleRun);
    connect(faultButton_, &QPushButton::clicked, this, &SimulatorWindow::injectFault);
    connect(recoverButton_, &QPushButton::clicked, this, &SimulatorWindow::injectRecovery);
    connect(refreshButton_, &QPushButton::clicked, this, &SimulatorWindow::refreshStatus);
    connect(resetButton_, &QPushButton::clicked, this, &SimulatorWindow::prepareReset);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &SimulatorWindow::onSelectionChanged);

    connect(client_, &ISimulatorClient::logMessage, this, &SimulatorWindow::onLog);
    connect(client_, &ISimulatorClient::connected, this, [this]() {
        badge_->setText(QStringLiteral("已连接"));
    });
    connect(client_, &ISimulatorClient::disconnected, this, [this]() {
        badge_->setText(QStringLiteral("未连接"));
    });
    connect(client_, &ISimulatorClient::chargersReceived,
            this, &SimulatorWindow::onChargersReceived);

    // R13: the panel starts paused; keep the client's reported state truthful.
    client_->setRunning(false);

    updateChargerTable();
}

QString SimulatorWindow::runButtonText() const
{
    return runButton_->text();
}

int SimulatorWindow::tickCount() const
{
    return tickCount_;
}

bool SimulatorWindow::faultEnabled() const
{
    return faultButton_->isEnabled();
}

bool SimulatorWindow::recoverEnabled() const
{
    return recoverButton_->isEnabled();
}

QStringList SimulatorWindow::logLines() const
{
    return logLines_;
}

void SimulatorWindow::toggleRun()
{
    running_ = !running_;
    client_->setRunning(running_);  // R13: keep the reported state truthful
    if (running_) {
        runButton_->setText(QStringLiteral("Pause"));
        tickTimer_->start();
        onLog(QStringLiteral("模拟器已启动"));
    } else {
        runButton_->setText(QStringLiteral("Run"));
        tickTimer_->stop();
        onLog(QStringLiteral("模拟器已暂停"));
    }
}

void SimulatorWindow::doTick()
{
    if (!running_)
        return;
    ++tickCount_;

    const QList<TelemetrySample> samples = engine_->tick();
    client_->sendTelemetry(samples);
    drainIntents();

    timeLabel_->setText(QStringLiteral("模拟时间：%1")
                            .arg(engine_->currentTime().toString(Qt::ISODate)));
    eventLabel_->setText(QStringLiteral("事件总数：%1").arg(tickCount_));
    updateChargerTable();
}

void SimulatorWindow::drainIntents()
{
    const QList<FaultIntent> intents = engine_->takePendingIntents();
    for (const FaultIntent &intent : intents)
        client_->sendFault(intent);
}

void SimulatorWindow::injectFault()
{
    const int id = selectedChargerId();
    if (id <= 0)
        return;
    if (engine_->requestFault(id)) {
        drainIntents();
        onLog(QStringLiteral("充电桩 %1 注入故障").arg(id));
        updateChargerTable();
    }
}

void SimulatorWindow::injectRecovery()
{
    const int id = selectedChargerId();
    if (id <= 0)
        return;
    if (engine_->requestRecovery(id)) {
        drainIntents();
        onLog(QStringLiteral("充电桩 %1 请求恢复").arg(id));
    }
}

void SimulatorWindow::refreshStatus()
{
    client_->refresh();
}

void SimulatorWindow::prepareReset()
{
    if (running_) {
        running_ = false;
        tickTimer_->stop();
        runButton_->setText(QStringLiteral("Run"));
        client_->setRunning(false);  // R13: report the paused state
    }
    onLog(QStringLiteral("请在管理端确认重置"));
}

void SimulatorWindow::onLog(const QString &message)
{
    logLines_.append(message);
    logList_->insertItem(0, message);
}

void SimulatorWindow::onChargersReceived(const QList<ChargerSnapshot> &chargers)
{
    Q_UNUSED(chargers);
    updateChargerTable();
}

void SimulatorWindow::onSelectionChanged()
{
    const bool hasSelection = selectedChargerId() > 0;
    faultButton_->setEnabled(hasSelection);
    recoverButton_->setEnabled(hasSelection);
}

void SimulatorWindow::updateChargerTable()
{
    const QList<ChargerSnapshot> chargers = engine_->chargers();
    table_->setRowCount(chargers.size());
    for (int i = 0; i < chargers.size(); ++i) {
        const ChargerSnapshot &c = chargers.at(i);
        table_->setItem(i, 0, new QTableWidgetItem(QString::number(c.chargerId)));
        table_->setItem(i, 1, new QTableWidgetItem(c.status));
        table_->setItem(i, 2, new QTableWidgetItem(QString::number(c.powerKw)));
    }
}

int SimulatorWindow::selectedChargerId() const
{
    const int row = table_->currentRow();
    if (row < 0 || row >= table_->rowCount())
        return 0;
    const QTableWidgetItem *item = table_->item(row, 0);
    return item ? item->text().toInt() : 0;
}

} // namespace ev::simulator
