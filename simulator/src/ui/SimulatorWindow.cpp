#include "ui/SimulatorWindow.h"
#include "ui/SimulatorTheme.h"
#include "ui/PulseChart.h"

#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace ev::simulator {
namespace {
constexpr int kHistoryLimit = 600;
constexpr int kLogLimit = 500;

QLabel *label(const QString &text, const char *role, QWidget *parent)
{
    auto *result = new QLabel(text, parent);
    result->setProperty("role", role);
    result->setTextFormat(Qt::PlainText);
    return result;
}

void tone(QWidget *widget, const QString &value)
{
    widget->setProperty("tone", value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

QFrame *rule(QWidget *parent)
{
    auto *result = new QFrame(parent);
    result->setObjectName(QStringLiteral("simRule"));
    result->setFixedHeight(1);
    return result;
}

QString statusText(const QString &status)
{
    if (status == QLatin1String("charging")) return QStringLiteral("充电中");
    if (status == QLatin1String("idle")) return QStringLiteral("空闲");
    if (status == QLatin1String("reserved")) return QStringLiteral("已预约");
    if (status == QLatin1String("fault")) return QStringLiteral("故障");
    if (status == QLatin1String("restarting")) return QStringLiteral("重启中");
    if (status == QLatin1String("offline")) return QStringLiteral("离线");
    return status;
}

QColor statusColor(const QString &status)
{
    if (status == QLatin1String("charging")) return QColor("#72e0c7");
    if (status == QLatin1String("fault")) return QColor("#f0af8e");
    if (status == QLatin1String("reserved")) return QColor("#e9bc87");
    return QColor("#97adbc");
}

// 列表用可读摘要，完整回执保存在 tooltip 和 logLines() 中。
QString logSummary(const QString &message)
{
    static const QRegularExpression event(
        QStringLiteral("^response (telemetry|fault)-(\\d+)-\\d+ (.+)$"));
    const auto match = event.match(message);
    if (match.hasMatch())
        return QStringLiteral("%1  ·  设备 %2  ·  %3").arg(
            match.captured(1) == QLatin1String("telemetry")
                ? QStringLiteral("遥测回执") : QStringLiteral("故障事件回执"),
            match.captured(2), match.captured(3));
    if (message.startsWith(QLatin1String("response sim-status-")))
        return QStringLiteral("设备状态同步  ·  %1").arg(message.section(QLatin1Char(' '), -1));
    if (message.startsWith(QLatin1String("connection error:")))
        return QStringLiteral("连接异常  ·  %1").arg(message.mid(17).trimmed());
    if (message == QLatin1String("refresh: not connected"))
        return QStringLiteral("未连接服务端，暂时无法同步状态");
    return message;
}
} // namespace

SimulatorWindow::SimulatorWindow(ISimulatorClient *client, TelemetryEngine *engine,
                                 QWidget *parent)
    : QWidget(parent), client_(client), engine_(engine)
{
    setWindowTitle(QStringLiteral("东软充电 · 设备模拟器"));
    setObjectName(QStringLiteral("simulatorWindow"));
    resize(1360, 860);
    setMinimumSize(1100, 680);
    applySimulatorTheme(this);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *rail = new QWidget(this);
    rail->setObjectName(QStringLiteral("simRail"));
    rail->setFixedWidth(228);
    auto *controls = new QVBoxLayout(rail);
    controls->setContentsMargins(20, 27, 20, 20);
    controls->setSpacing(10);
    controls->addWidget(label(QStringLiteral("东软充电"), "brand", rail));
    controls->addWidget(label(QStringLiteral("设备模拟工作台"), "muted", rail));
    controls->addSpacing(24);
    controls->addWidget(label(QStringLiteral("模拟进程"), "muted", rail));
    runState_ = label(QStringLiteral("已暂停"), "state", rail);
    runState_->setObjectName(QStringLiteral("simRunState"));
    controls->addWidget(runState_);
    controls->addWidget(label(QStringLiteral("采样间隔   %1 s")
        .arg(engine_->intervalMs() / 1000.0, 0, 'g', 5), "muted", rail));
    runButton_ = new QPushButton(QStringLiteral("启动模拟"), rail);
    runButton_->setObjectName(QStringLiteral("simRunButton"));
    runButton_->setProperty("role", "primary");
    runButton_->setMinimumHeight(44);
    controls->addWidget(runButton_);
    controls->addSpacing(14);
    controls->addWidget(rule(rail));
    controls->addSpacing(10);
    controls->addWidget(label(QStringLiteral("选中设备 / 故障演练"), "muted", rail));
    selectedLabel_ = label(QStringLiteral("未选择"), "state", rail);
    selectedLabel_->setObjectName(QStringLiteral("simSelectedCharger"));
    controls->addWidget(selectedLabel_);
    selectedState_ = label(QStringLiteral("点击设备表中的一行"), "muted", rail);
    controls->addWidget(selectedState_);
    selectedRated_ = label(QStringLiteral("额定功率    —"), "muted", rail);
    selectedPower_ = label(QStringLiteral("最新采样    —"), "muted", rail);
    controls->addWidget(selectedRated_);
    controls->addWidget(selectedPower_);
    controls->addSpacing(4);
    faultButton_ = new QPushButton(QStringLiteral("故障注入"), rail);
    faultButton_->setObjectName(QStringLiteral("simFaultButton"));
    faultButton_->setProperty("role", "danger");
    recoverButton_ = new QPushButton(QStringLiteral("请求恢复"), rail);
    recoverButton_->setObjectName(QStringLiteral("simRecoverButton"));
    controls->addWidget(faultButton_);
    controls->addWidget(recoverButton_);
    controls->addWidget(label(QStringLiteral("恢复事件不等于完成重启；\n最终状态由服务端确认。"), "micro", rail));
    controls->addStretch(1);
    controls->addWidget(rule(rail));
    resetButton_ = new QPushButton(QStringLiteral("准备复位"), rail);
    resetButton_->setObjectName(QStringLiteral("simResetButton"));
    controls->addWidget(resetButton_);
    controls->addWidget(label(QStringLiteral("这里只暂停模拟。数据复位请在\n管理端确认，不会直接修改数据库。"), "micro", rail));
    root->addWidget(rail);

    auto *main = new QVBoxLayout;
    main->setContentsMargins(28, 25, 28, 18);
    main->setSpacing(16);
    auto *header = new QHBoxLayout;
    auto *heading = new QVBoxLayout;
    heading->setSpacing(4);
    heading->addWidget(label(QStringLiteral("遥测实验台"), "title", this));
    heading->addWidget(label(QStringLiteral("设备模拟器  /  本地遥测与故障演练"), "muted", this));
    header->addLayout(heading);
    header->addStretch();
    badge_ = label(QStringLiteral("未连接"), "badge", this);
    badge_->setObjectName(QStringLiteral("simSessionBadge"));
    header->addWidget(badge_);
    refreshButton_ = new QPushButton(QStringLiteral("同步设备状态"), this);
    refreshButton_->setObjectName(QStringLiteral("simRefreshButton"));
    header->addWidget(refreshButton_);
    main->addLayout(header);
    main->addWidget(rule(this));

    auto *viewport = new QScrollArea(this);
    viewport->setObjectName(QStringLiteral("simViewport"));
    viewport->setWidgetResizable(true);
    viewport->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget(viewport);
    body->setObjectName(QStringLiteral("simBody"));
    body->setMinimumHeight(648);
    auto *content = new QVBoxLayout(body);
    content->setContentsMargins(0, 0, 8, 0);
    content->setSpacing(10);
    auto *metrics = new QHBoxLayout;
    auto *powerBlock = new QVBoxLayout;
    scopeLabel_ = label(QStringLiteral("所有设备 / 本地采样功率"), "muted", body);
    scopeLabel_->setObjectName(QStringLiteral("simChartScope"));
    powerBlock->addWidget(scopeLabel_);
    auto *powerLine = new QHBoxLayout;
    powerLine->setSpacing(7);
    powerLabel_ = label(QStringLiteral("—"), "power", body);
    powerLabel_->setObjectName(QStringLiteral("simPowerValue"));
    powerLabel_->setFixedHeight(60);
    powerLine->addWidget(powerLabel_);
    powerLine->addWidget(label(QStringLiteral("kW"), "unit", body), 0, Qt::AlignBottom);
    powerLine->addStretch();
    powerBlock->addLayout(powerLine);
    metrics->addLayout(powerBlock, 1);
    auto addMetric = [&](const QString &caption, const char *name) {
        auto *column = new QVBoxLayout;
        column->setSpacing(3);
        column->addWidget(label(caption, "muted", body));
        auto *value = label(QStringLiteral("0"), "number", body);
        value->setObjectName(QString::fromLatin1(name));
        column->addWidget(value);
        metrics->addLayout(column);
        metrics->addSpacing(26);
        return value;
    };
    eventLabel_ = addMetric(QStringLiteral("运行节拍"), "simBatchCount");
    sampleLabel_ = addMetric(QStringLiteral("本地生成条数"), "simSampleCount");
    content->addLayout(metrics);
    auto *chartToolbar = new QHBoxLayout;
    cursorLabel_ = label(QStringLiteral("等待首次采样"), "muted", body);
    cursorLabel_->setObjectName(QStringLiteral("simCursorLabel"));
    chartToolbar->addWidget(cursorLabel_);
    chartToolbar->addStretch();
    auto *all = new QPushButton(QStringLiteral("全部设备"), body);
    all->setObjectName(QStringLiteral("simAllDevicesButton"));
    auto *latest = new QPushButton(QStringLiteral("回到最新采样"), body);
    latest->setObjectName(QStringLiteral("simLatestButton"));
    chartToolbar->addWidget(all);
    chartToolbar->addWidget(latest);
    content->addLayout(chartToolbar);
    chart_ = new ev::ui::PulseChart(false, body);
    chart_->setPreserveAspectRatio(false);
    chart_->setObjectName(QStringLiteral("simPowerChart"));
    chart_->setFixedHeight(250);
    content->addWidget(chart_);
    content->addWidget(label(QStringLiteral("曲线是本地生成样本，不代表服务端已接收；拖动可回看，回执见下方日志。"), "micro", body));
    content->addWidget(rule(body));

    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(24);
    auto *fleet = new QVBoxLayout;
    fleet->setSpacing(9);
    auto *fleetHeader = new QHBoxLayout;
    fleetHeader->addWidget(label(QStringLiteral("设备阵列"), "section", body));
    fleetHeader->addStretch();
    fleetLabel_ = label(QString(), "micro", body);
    fleetHeader->addWidget(fleetLabel_);
    fleet->addLayout(fleetHeader);
    table_ = new QTableWidget(0, 4, body);
    table_->setObjectName(QStringLiteral("simChargerTable"));
    table_->setHorizontalHeaderLabels({QStringLiteral("桩 ID"), QStringLiteral("设备状态"),
                                      QStringLiteral("额定 / kW"), QStringLiteral("采样 / kW")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(38);
    table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setMinimumHeight(178);
    fleet->addWidget(table_, 1);
    emptyLabel_ = label(QStringLiteral("尚无设备，连接并通过鉴权后同步列表。"), "muted", body);
    fleet->addWidget(emptyLabel_);
    bottom->addLayout(fleet, 3);
    auto *events = new QVBoxLayout;
    events->setSpacing(9);
    auto *logHeader = new QHBoxLayout;
    logHeader->addWidget(label(QStringLiteral("事件回执"), "section", body));
    logHeader->addStretch();
    logHeader->addWidget(label(QStringLiteral("最近 500 条 / 悬停查看原文"), "micro", body));
    events->addLayout(logHeader);
    logList_ = new QListWidget(body);
    logList_->setObjectName(QStringLiteral("simEventLog"));
    logList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    logList_->setTextElideMode(Qt::ElideRight);
    logList_->setMinimumHeight(178);
    events->addWidget(logList_, 1);
    logEmptyLabel_ = label(QStringLiteral("启动后在此查看本地操作与服务端回执。"), "muted", body);
    events->addWidget(logEmptyLabel_);
    bottom->addLayout(events, 2);
    content->addLayout(bottom, 1);
    viewport->setWidget(body);
    main->addWidget(viewport, 1);
    auto *footer = new QHBoxLayout;
    timeLabel_ = label(QStringLiteral("模拟时钟  —"), "micro", this);
    timeLabel_->setObjectName(QStringLiteral("simClock"));
    footer->addWidget(timeLabel_);
    footer->addStretch();
    footer->addWidget(label(QStringLiteral("本地样本仅用于演练  ·  订单与计费以服务端为准"), "micro", this));
    main->addLayout(footer);
    root->addLayout(main, 1);

    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(engine_->intervalMs());
    connect(tickTimer_, &QTimer::timeout, this, &SimulatorWindow::doTick);
    connect(runButton_, &QPushButton::clicked, this, &SimulatorWindow::toggleRun);
    connect(faultButton_, &QPushButton::clicked, this, &SimulatorWindow::injectFault);
    connect(recoverButton_, &QPushButton::clicked, this, &SimulatorWindow::injectRecovery);
    connect(refreshButton_, &QPushButton::clicked, this, &SimulatorWindow::refreshStatus);
    connect(resetButton_, &QPushButton::clicked, this, &SimulatorWindow::prepareReset);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &SimulatorWindow::onSelectionChanged);
    connect(all, &QPushButton::clicked, this, [this] {
        const QSignalBlocker blocker(table_);
        table_->clearSelection();
        table_->setCurrentCell(-1, -1);
        onSelectionChanged();
    });
    connect(latest, &QPushButton::clicked, chart_, &ev::ui::PulseChart::followLatest);
    connect(chart_, &ev::ui::PulseChart::cursorChanged, this, &SimulatorWindow::updateReading);
    connect(client_, &ISimulatorClient::logMessage, this, &SimulatorWindow::onLog);
    connect(client_, &ISimulatorClient::connected, this, [this] {
        setSessionState(QStringLiteral("等待鉴权"), QStringLiteral("muted"));
    });
    connect(client_, &ISimulatorClient::disconnected, this, [this] {
        setSessionState(QStringLiteral("未连接"), QStringLiteral("warn"));
    });
    connect(client_, &ISimulatorClient::sessionReady, this, [this] {
        setSessionState(QStringLiteral("已接入"), QStringLiteral("good"));
    });
    connect(client_, &ISimulatorClient::authenticationFailed, this, [this](const QString &) {
        setSessionState(QStringLiteral("鉴权失败"), QStringLiteral("warn"));
    });
    connect(client_, &ISimulatorClient::chargersReceived, this, &SimulatorWindow::onChargersReceived);
    client_->setRunning(false); // 窗口初始暂停，不虚报 simulator.status。
    updateRunState();
    updateChargerTable();
}

QString SimulatorWindow::runButtonText() const { return runButton_->text(); }
int SimulatorWindow::tickCount() const { return tickCount_; }
bool SimulatorWindow::faultEnabled() const { return faultButton_->isEnabled(); }
bool SimulatorWindow::recoverEnabled() const { return recoverButton_->isEnabled(); }
QStringList SimulatorWindow::logLines() const { return logLines_; }

void SimulatorWindow::setSessionState(const QString &text, const QString &value)
{
    badge_->setText(text);
    tone(badge_, value);
    badge_->setToolTip(QStringLiteral("接入状态与本地采样是否运行相互独立；本地曲线不是入库确认。"));
}

void SimulatorWindow::updateRunState()
{
    runButton_->setText(running_ ? QStringLiteral("暂停模拟") : QStringLiteral("启动模拟"));
    runState_->setText(running_ ? QStringLiteral("运行中") : QStringLiteral("已暂停"));
    tone(runState_, running_ ? QStringLiteral("good") : QStringLiteral("muted"));
    updateReading();
}

void SimulatorWindow::toggleRun()
{
    running_ = !running_;
    client_->setRunning(running_);
    if (running_) tickTimer_->start();
    else tickTimer_->stop();
    updateRunState();
    onLog(running_ ? QStringLiteral("模拟器已启动") : QStringLiteral("模拟器已暂停"));
}

// 采样节拍：引擎生成一批遥测 → 发给服务端 → 存入本地环形缓存（供曲线回看）。
void SimulatorWindow::doTick()
{
    if (!running_) return;
    ++tickCount_;
    const QList<TelemetrySample> samples = engine_->tick();
    client_->sendTelemetry(samples);
    drainIntents();
    if (!samples.isEmpty()) {
        Batch batch;   // 一批 = 同一时刻所有桩的采样；只保留最近 600 批
        batch.recordedAt = samples.first().recordedAt;
        for (const auto &sample : samples) {
            batch.samples.insert(sample.chargerId, sample);
            latestSamples_.insert(sample.chargerId, sample);
        }
        batches_.append(batch);
        if (batches_.size() > kHistoryLimit) batches_.removeFirst();
        sampleCount_ += samples.size();
    }
    timeLabel_->setText(QStringLiteral("模拟时钟  %1  /  +08:00")
        .arg(engine_->currentTime().toOffsetFromUtc(8 * 3600).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    eventLabel_->setText(QString::number(tickCount_));
    sampleLabel_->setText(QString::number(sampleCount_));
    updateChargerTable();
}

void SimulatorWindow::drainIntents()
{
    for (const FaultIntent &intent : engine_->takePendingIntents()) client_->sendFault(intent);
}

void SimulatorWindow::injectFault()
{
    const int id = selectedChargerId();
    if (id > 0 && engine_->requestFault(id)) {
        drainIntents();
        onLog(QStringLiteral("设备 %1 已生成故障事件，等待服务端回执").arg(id));
        updateChargerTable();
    }
}

void SimulatorWindow::injectRecovery()
{
    const int id = selectedChargerId();
    if (id > 0 && engine_->requestRecovery(id)) {
        drainIntents();
        onLog(QStringLiteral("设备 %1 已请求恢复，最终状态待服务端同步").arg(id));
    }
}

void SimulatorWindow::refreshStatus() { client_->refresh(); }

void SimulatorWindow::prepareReset()
{
    running_ = false;
    tickTimer_->stop();
    client_->setRunning(false);
    updateRunState();
    onLog(QStringLiteral("已暂停；请在管理端确认重置，本窗口不会修改数据库"));
}

void SimulatorWindow::onLog(const QString &message)
{
    logLines_.append(message);
    if (logLines_.size() > kLogLimit) logLines_.removeFirst();
    auto *item = new QListWidgetItem(logSummary(message));
    item->setToolTip(message);
    if (message.startsWith(QLatin1String("response ")))
        item->setForeground(message.endsWith(QLatin1String(" OK")) ? QColor("#72e0c7") : QColor("#f0af8e"));
    else if (message.contains(QLatin1String("error"), Qt::CaseInsensitive)
             || message.contains(QLatin1String("dropped"))) item->setForeground(QColor("#f0af8e"));
    logList_->insertItem(0, item);
    while (logList_->count() > kLogLimit) delete logList_->takeItem(logList_->count() - 1);
    logEmptyLabel_->hide();
}

void SimulatorWindow::onChargersReceived(const QList<ChargerSnapshot> &chargers)
{
    Q_UNUSED(chargers); // SimulatorClient 已先将权威快照同步到 engine。
    updateChargerTable();
}

void SimulatorWindow::onSelectionChanged()
{
    const int id = selectedChargerId();
    faultButton_->setEnabled(false);
    recoverButton_->setEnabled(false);
    selectedLabel_->setText(id > 0 ? QString::number(id) : QStringLiteral("未选择"));
    selectedState_->setText(QStringLiteral("点击设备表中的一行"));
    selectedRated_->setText(QStringLiteral("额定功率    —"));
    selectedPower_->setText(QStringLiteral("最新采样    —"));
    selectedPower_->setToolTip(QString());
    tone(selectedState_, QStringLiteral("muted"));
    for (const auto &charger : engine_->chargers()) {
        if (charger.chargerId != id) continue;
        selectedState_->setText(statusText(charger.status));
        tone(selectedState_, charger.status == QLatin1String("fault") ? QStringLiteral("warn") : QStringLiteral("good"));
        selectedRated_->setText(QStringLiteral("额定功率    %1 kW").arg(charger.powerKw, 0, 'g', 5));
        const auto sample = latestSamples_.constFind(id);
        if (sample != latestSamples_.cend()) {
            selectedPower_->setText(QStringLiteral("最新采样    %1 kW").arg(sample->powerKw, 0, 'f', 1));
            selectedPower_->setToolTip(QStringLiteral("本地样本 %1；不是当前设备额定功率")
                .arg(sample->recordedAt.toString(Qt::ISODateWithMs)));
        }
        faultButton_->setEnabled(charger.status == QLatin1String("idle") || charger.status == QLatin1String("reserved") || charger.status == QLatin1String("charging"));
        recoverButton_->setEnabled(charger.status == QLatin1String("fault"));
        break;
    }
    updateChart();
}

// 设备表刷新：数据源是 engine 里的服务端权威快照；刷新前后保持选中行和滚动位置不变。
void SimulatorWindow::updateChargerTable()
{
    const int previousId = selectedChargerId();
    const int scrollPosition = table_->verticalScrollBar()->value();
    const auto chargers = engine_->chargers();
    int selectedRow = -1, charging = 0, faults = 0;
    QMap<int, TelemetrySample> currentSamples;
    {
        const QSignalBlocker blocker(table_);
        table_->setRowCount(chargers.size());
        for (int row = 0; row < chargers.size(); ++row) {
            const auto &charger = chargers.at(row);
            if (charger.chargerId == previousId) selectedRow = row;
            charging += charger.status == QLatin1String("charging");
            faults += charger.status == QLatin1String("fault");
            const auto sample = latestSamples_.constFind(charger.chargerId);
            QString power = QStringLiteral("—");
            if (sample != latestSamples_.cend()) {
                power = QString::number(sample->powerKw, 'f', 1);
                currentSamples.insert(sample.key(), sample.value());
            }
            const QStringList values{QString::number(charger.chargerId), statusText(charger.status),
                QString::number(charger.powerKw, 'g', 5), power};
            for (int column = 0; column < values.size(); ++column) {
                auto *item = table_->item(row, column);
                if (!item) { item = new QTableWidgetItem; table_->setItem(row, column, item); }
                item->setText(values.at(column));
                item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                item->setForeground(column == 1 ? statusColor(charger.status) : QColor("#c6d8e2"));
                item->setToolTip(column == 3 && sample != latestSamples_.cend()
                    ? QStringLiteral("最新本地采样：%1\n不是服务端入库确认，也不是额定功率。")
                        .arg(sample->recordedAt.toString(Qt::ISODateWithMs)) : values.at(column));
            }
        }
        table_->clearSelection();
        if (selectedRow >= 0) table_->selectRow(selectedRow);
        else table_->setCurrentCell(-1, -1);
        table_->verticalScrollBar()->setValue(scrollPosition);
    }
    latestSamples_ = currentSamples;   // 只保留本轮有采样的桩，避免显示陈旧数据
    fleetLabel_->setText(QStringLiteral("%1 台  /  充电 %2  /  故障 %3").arg(chargers.size()).arg(charging).arg(faults));
    emptyLabel_->setVisible(chargers.isEmpty());
    onSelectionChanged();
}

// 曲线刷新：选中设备画单桩功率，未选中画全场合计；历史回看时按原时间戳还原光标位置。
void SimulatorWindow::updateChart()
{
    const int id = selectedChargerId();
    const bool pinned = !chart_->followingLatest();
    const qint64 cursorMs = chartStartMs_ + qRound64(chart_->cursorSeconds() * 1000);
    ev::ui::PowerSeries series;
    series.id = QString::number(id);
    series.name = id > 0 ? QStringLiteral("设备 %1").arg(id) : QStringLiteral("所有设备");
    scopeLabel_->setText(QStringLiteral("%1 / 本地采样功率").arg(series.name));
    chartStartMs_ = batches_.isEmpty() ? 0 : batches_.first().recordedAt.toMSecsSinceEpoch();
    for (const auto &batch : batches_) {
        if (id > 0 && !batch.samples.contains(id)) continue;
        double power = 0;
        if (id > 0) power = batch.samples.value(id).powerKw;
        else for (const auto &sample : batch.samples) power += sample.powerKw;   // 合计模式：各桩求和
        series.points.append({(batch.recordedAt.toMSecsSinceEpoch() - chartStartMs_) / 1000.0, power, 0});
    }
    const double duration = batches_.isEmpty() ? 60.0
        : qMax(1.0, (batches_.last().recordedAt.toMSecsSinceEpoch() - chartStartMs_) / 1000.0);
    chart_->setSeries({series}, duration, 60);
    chart_->setMessage(series.points.isEmpty()
        ? QStringLiteral("等待本地采样\n同步设备后启动模拟，曲线将随实际生成数据出现") : QString());
    if (pinned) chart_->setCursorSeconds((cursorMs - chartStartMs_) / 1000.0);
    updateReading();
}

void SimulatorWindow::updateReading()
{
    const auto reading = chart_->reading(0);
    powerLabel_->setText(reading ? QString::number(reading->kw, 'f', 1) : QStringLiteral("—"));
    if (batches_.isEmpty()) { cursorLabel_->setText(QStringLiteral("等待首次采样")); return; }
    const qint64 whenMs = chart_->followingLatest() ? batches_.last().recordedAt.toMSecsSinceEpoch()
        : chartStartMs_ + qRound64(chart_->cursorSeconds() * 1000);
    const auto when = QDateTime::fromMSecsSinceEpoch(whenMs)
        .toOffsetFromUtc(8 * 3600).toString(QStringLiteral("HH:mm:ss"));
    cursorLabel_->setText(QStringLiteral("%1  %2  /  最近 %3 批")
        .arg(chart_->followingLatest() ? (running_ ? QStringLiteral("最新样本") : QStringLiteral("已暂停 · 保留样本")) : QStringLiteral("历史回看"),
             when).arg(batches_.size()));
}

int SimulatorWindow::selectedChargerId() const
{
    const int row = table_->currentRow();
    if (row < 0 || row >= table_->rowCount() || !table_->selectionModel()->isRowSelected(row, QModelIndex())) return 0;
    const auto *item = table_->item(row, 0);
    return item ? item->text().toInt() : 0;
}
} // namespace ev::simulator
