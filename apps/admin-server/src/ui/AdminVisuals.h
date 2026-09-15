#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStyledItemDelegate>
#include <QWidget>

namespace AdminVisuals {
QString statusText(const QString &code);
QColor statusColor(const QString &code);

class StatusDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
};

class RevenueChart final : public QWidget
{
public:
    explicit RevenueChart(QWidget *parent = nullptr);
    void setPoints(const QJsonArray &points);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QJsonArray m_points;
};

class StateRing final : public QWidget
{
public:
    explicit StateRing(QWidget *parent = nullptr);
    void setCounts(const QJsonObject &counts);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QJsonObject m_counts;
};
}
