#pragma once

#include "domain/Models.h"

#include <QDate>
#include <QPainter>
#include <QWidget>

// 七日个人用电小图：仅呈现服务端汇总值；未加载和真实零用电是不同状态。
class UsageChart final : public QWidget
{
public:
    explicit UsageChart(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("profileUsageChart"));
        setFixedHeight(164);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setAccessibleName(QStringLiteral("近七天用电量柱状图"));
    }

    void setDays(const QVector<ev::user::DailyUsage> &days)
    {
        days_ = days;
        QStringList details;
        for (const auto &day : days_) details.append(QStringLiteral("%1：%2 kWh").arg(day.date).arg(day.energyKwh, 0, 'f', 3));
        setAccessibleDescription(details.isEmpty() ? QStringLiteral("暂无统计数据") : details.join(QStringLiteral("；")));
        setToolTip(accessibleDescription());
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont labelFont = font();
        labelFont.setPixelSize(11);
        painter.setFont(labelFont);
        if (days_.isEmpty()) {
            painter.setPen(QColor("#97adbc"));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("用电统计尚未加载"));
            return;
        }
        double peak = 0.1;
        for (const auto &day : days_) peak = qMax(peak, day.energyKwh);
        const qreal bottom = height() - 25;
        const qreal graphHeight = bottom - 26;
        const qreal column = qreal(width()) / days_.size();
        painter.setPen(QColor("#314d5d"));
        painter.drawLine(QPointF(0, bottom), QPointF(width(), bottom));
        for (int i = 0; i < days_.size(); ++i) {
            const auto &day = days_[i];
            const qreal x = i * column;
            const qreal barHeight = graphHeight * day.energyKwh / peak;
            const qreal barWidth = qMin<qreal>(22, column * 0.5);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(i == days_.size() - 1 ? "#a2f3df" : "#58baa7"));
            if (barHeight > 0) painter.drawRoundedRect(QRectF(x + (column - barWidth) / 2,
                bottom - qMax<qreal>(2, barHeight), barWidth, qMax<qreal>(2, barHeight)), 4, 4);
            else painter.drawEllipse(QPointF(x + column / 2, bottom - 3), 2, 2);
            painter.setPen(QColor("#97adbc"));
            painter.drawText(QRectF(x, bottom + 5, column, 18), Qt::AlignCenter,
                QDate::fromString(day.date, Qt::ISODate).toString(QStringLiteral("MM/dd")));
            painter.setPen(QColor("#d2e7e9"));
            const QString value = day.energyKwh >= 1000
                ? QString::number(day.energyKwh / 1000, 'f', 1) + QStringLiteral("k")
                : QString::number(day.energyKwh, 'f', day.energyKwh > 0 && day.energyKwh < 10 ? 2 : 1);
            painter.drawText(QRectF(x, bottom - barHeight - 23, column, 20), Qt::AlignCenter, value);
        }
    }

private:
    QVector<ev::user::DailyUsage> days_;
};
