#include "ui/HealthPanel.h"

#include "app/AppContext.h"
#include "services/ForecastService.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

HealthPanel::HealthPanel(AppContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("系统健康自检"));
    QFont titleFont = title->font();
    titleFont.setPointSize(15);
    titleFont.setBold(true);
    title->setFont(titleFont);
    rootLayout->addWidget(title);

    m_serverTimeLabel = new QLabel;
    m_serverTimeLabel->setStyleSheet(QStringLiteral("color:#757575;"));
    rootLayout->addWidget(m_serverTimeLabel);

    auto *refreshButton = new QPushButton(QStringLiteral("刷新"));
    connect(refreshButton, &QPushButton::clicked, this, [this]() { rebuild(); });
    rootLayout->addWidget(refreshButton);

    m_checksLayout = new QVBoxLayout;
    m_checksLayout->setSpacing(4);
    rootLayout->addLayout(m_checksLayout);
    rootLayout->addStretch();

    rebuild();
}

QList<HealthPanel::CheckItem> HealthPanel::collectChecks() const
{
    // 数据来源仅 system.health（ForecastService::healthState()）既有字段，
    // 遵守 F5 边界：不直接访问业务库、不为面板塞入额外字段；拿不到的信息
    // 标"未验证"；forecastRunId=null / 无快照属合法降级，不判核心失败。
    const QJsonObject health = m_context->forecastService()->healthState();

    const QString status = health.value(QStringLiteral("status")).toString();
    const bool serviceOk = (status == QStringLiteral("ready"));

    const QString serverTime = health.value(QStringLiteral("serverTime")).toString();

    const int snapshotVersion = health.value(QStringLiteral("snapshotVersion")).toInt();

    const QJsonValue runIdValue = health.value(QStringLiteral("forecastRunId"));
    const QString runId = runIdValue.isString() ? runIdValue.toString().trimmed() : QString();

    QList<CheckItem> checks;

    // —— 核心区 ——
    checks.append(CheckItem{
        QStringLiteral("服务状态"),
        serviceOk ? CheckStatus::Pass : CheckStatus::Fail,
        serviceOk
            ? QStringLiteral("服务运行正常（%1）").arg(status)
            : QStringLiteral("服务状态异常（%1）").arg(status),
        false
    });

    checks.append(CheckItem{
        QStringLiteral("模拟器连接状态"),
        CheckStatus::Pending,
        QStringLiteral("未验证：system.health 未提供模拟器连接/运行状态来源，需 #2 补充数据源后接入"),
        false
    });

    // —— 可选诊断区（不影响核心验收）——
    checks.append(CheckItem{
        QStringLiteral("快照版本"),
        snapshotVersion > 0 ? CheckStatus::Pass : CheckStatus::Pending,
        snapshotVersion > 0
            ? QStringLiteral("当前快照版本 v%1").arg(snapshotVersion)
            : QStringLiteral("未启用：尚无快照（snapshotVersion=0），不判定核心失败"),
        true
    });

    checks.append(CheckItem{
        QStringLiteral("活动预测批次"),
        !runId.isEmpty() ? CheckStatus::Pass : CheckStatus::Pending,
        !runId.isEmpty()
            ? QStringLiteral("活动预测批次 %1").arg(runId)
            : QStringLiteral("未启用：无活动预测（forecastRunId=null），属合法降级，不判定核心失败"),
        true
    });

    checks.append(CheckItem{
        QStringLiteral("预测数据新鲜度"),
        CheckStatus::Pending,
        QStringLiteral("未验证：system.health 未暴露 activatedAt，需 #2 扩展接口后方可判断"),
        true
    });

    return checks;
}

QWidget *HealthPanel::renderCheck(const CheckItem &item) const
{
    auto *frame = new QFrame;
    frame->setFrameShape(QFrame::StyledPanel);

    auto *statusLabel = new QLabel;
    switch (item.status) {
    case CheckStatus::Pass:
        statusLabel->setText(QStringLiteral("正常"));
        statusLabel->setStyleSheet(QStringLiteral("color:#2e7d32; font-weight:bold;"));
        break;
    case CheckStatus::Fail:
        statusLabel->setText(QStringLiteral("异常"));
        statusLabel->setStyleSheet(QStringLiteral("color:#c62828; font-weight:bold;"));
        break;
    case CheckStatus::Pending:
        statusLabel->setText(QStringLiteral("未验证"));
        statusLabel->setStyleSheet(QStringLiteral("color:#757575; font-weight:bold;"));
        break;
    }
    statusLabel->setMinimumWidth(52);

    auto *nameLabel = new QLabel(item.name);
    nameLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    nameLabel->setMinimumWidth(150);

    auto *detailLabel = new QLabel(item.detail);
    detailLabel->setWordWrap(true);

    auto *layout = new QHBoxLayout(frame);
    layout->addWidget(statusLabel);
    layout->addWidget(nameLabel);
    layout->addWidget(detailLabel, 1);
    return frame;
}

void HealthPanel::rebuild()
{
    while (QLayoutItem *item = m_checksLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    const QJsonObject health = m_context->forecastService()->healthState();
    const QString serverTime = health.value(QStringLiteral("serverTime")).toString();
    m_serverTimeLabel->setText(serverTime.isEmpty()
        ? QStringLiteral("检查时间：未知")
        : QStringLiteral("检查时间：%1").arg(serverTime));

    const QList<CheckItem> checks = collectChecks();
    bool optionalHeaderAdded = false;
    for (const CheckItem &check : checks) {
        if (check.optional && !optionalHeaderAdded) {
            auto *header = new QLabel(QStringLiteral("可选诊断（不影响核心验收）"));
            header->setStyleSheet(QStringLiteral("color:#757575; margin-top:4px;"));
            m_checksLayout->addWidget(header);
            optionalHeaderAdded = true;
        }
        m_checksLayout->addWidget(renderCheck(check));
    }
}
