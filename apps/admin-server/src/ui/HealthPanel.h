#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class AppContext;
class QLabel;
class QVBoxLayout;

// 系统健康自检面板（F5）。
//
// 数据来源只使用 system.health（ForecastService::healthState()）既有字段，
// 不直接访问业务库。快照/预测等 optional 信息放在独立诊断区，缺省状态
// （forecastRunId=null、无快照）属合法降级，不判定核心验收失败。
// 设计要点：数据获取与 UI 渲染分离，后续改样式或对接真实数据源时只需动
// 对应的一处方法，不影响其余部分。
class HealthPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HealthPanel(AppContext *context, QWidget *parent = nullptr);

private:
    enum class CheckStatus
    {
        Pass,     // 绿：正常
        Fail,     // 红：异常
        Pending   // 灰：未验证 / 未启用 / 暂无数据
    };

    struct CheckItem
    {
        QString name;
        CheckStatus status = CheckStatus::Pending;
        QString detail;
        bool optional = false; // true 表示属于可选诊断区，不影响核心验收
    };

    // 数据层：把健康信息整理成检查项列表。后续对接新数据源只改这里。
    QList<CheckItem> collectChecks() const;

    // 渲染层：把单个检查项渲染成一行。后续改 UI 只改这里。
    QWidget *renderCheck(const CheckItem &item) const;

    void rebuild();

    AppContext *m_context = nullptr;
    QVBoxLayout *m_checksLayout = nullptr;
    QLabel *m_serverTimeLabel = nullptr;
};
