#include "ui/AdminVisuals.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QSet>
#include <QtMath>

namespace AdminVisuals {
namespace {
const QStringList codes={"idle","reserved","charging","fault","restarting"};
}

QString statusText(const QString &code)
{
    static const QHash<QString,QString> labels={
        {"idle",QStringLiteral("空闲")},{"reserved",QStringLiteral("已预约")},
        {"charging",QStringLiteral("充电中")},{"fault",QStringLiteral("故障")},
        {"restarting",QStringLiteral("重启中")},{"active",QStringLiteral("正常")},
        {"frozen",QStringLiteral("已冻结")},{"fast",QStringLiteral("快充")},
        {"slow",QStringLiteral("慢充")},{"OK",QStringLiteral("成功")}};
    return labels.value(code);
}

QColor statusColor(const QString &code)
{
    if(code=="fault" || code=="frozen") return QColor("#B94B43");
    if(code=="reserved" || code=="restarting") return QColor("#A86619");
    if(code=="charging" || code=="fast") return QColor("#477991");
    return QColor("#00856A");
}

void StatusDelegate::paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index) const
{
    const QString label=index.data(Qt::AccessibleTextRole).toString();
    if(!index.data(Qt::UserRole+1).toBool() || label.isEmpty()) {
        QStyledItemDelegate::paint(painter,option,index);
        return;
    }
    QStyleOptionViewItem background(option);
    initStyleOption(&background,index);
    background.text.clear();
    const QWidget *widget=option.widget;
    (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem,&background,painter,widget);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor color=statusColor(index.data().toString());
    const int width=qMin(option.rect.width()-12,option.fontMetrics.horizontalAdvance(label)+26);
    const QRect pill(option.rect.center().x()-width/2,option.rect.center().y()-13,width,26);
    QColor tint=color; tint.setAlpha(24);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tint);
    painter->drawRoundedRect(pill,6,6);
    painter->setPen(color);
    painter->drawText(pill,Qt::AlignCenter,label);
    painter->restore();
}

RevenueChart::RevenueChart(QWidget *parent):QWidget(parent)
{
    setMinimumSize(280,126);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("营收趋势折线图"));
}
void RevenueChart::setPoints(const QJsonArray &points)
{
    m_points=points;
    setAccessibleDescription(QStringLiteral("最近 %1 天的已完成订单营收，完整数值可查阅下方营收明细表").arg(points.size()));
    update();
}
void RevenueChart::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    QFont small=font(); small.setPixelSize(11); p.setFont(small);
    const QRectF plot(55,16,width()-72,height()-49);
    double highest=0;
    for(const auto &value:m_points) highest=qMax(highest,value.toObject().value("revenueFen").toDouble()/100.0);
    const double ceiling=highest>0 ? qCeil(highest/4.0)*4.0 : 100.0;
    for(int tick=0;tick<=4;++tick) {
        const qreal y=plot.bottom()-plot.height()*tick/4.0;
        p.setPen(QPen(QColor("#E2E8E1"),1,Qt::DashLine));
        p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
        p.setPen(QColor("#718078"));
        p.drawText(QRectF(0,y-8,45,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(ceiling*tick/4,'f',0));
    }
    if(m_points.isEmpty()) {
        p.drawText(plot,Qt::AlignCenter,QStringLiteral("正在读取营收数据…")); return;
    }
    QPainterPath line;
    QList<QPointF> points;
    // 稀疏且均匀的刻度，避免 30 日模式下最后两天的标签重叠。
    QSet<int> ticks;
    const int tickCount=qMin(m_points.size(),qMax(2,int(plot.width()/75)+1));
    for(int tick=0;tick<tickCount;++tick)
        ticks.insert(qRound(double(tick)*(m_points.size()-1)/qMax(1,tickCount-1)));
    for(int i=0;i<m_points.size();++i) {
        const auto row=m_points.at(i).toObject();
        const QPointF point(plot.left()+plot.width()*i/qMax(1,m_points.size()-1),
                            plot.bottom()-plot.height()*(row.value("revenueFen").toDouble()/100.0)/ceiling);
        points.append(point);
        if(i==0) line.moveTo(point); else line.lineTo(point);
        if(ticks.contains(i)) {
            p.setPen(QColor("#718078"));
            p.drawText(QRectF(point.x()-28,plot.bottom()+9,56,20),Qt::AlignCenter,row.value("date").toString().mid(5).replace('-','/'));
        }
    }
    QPainterPath area=line;
    area.lineTo(points.last().x(),plot.bottom()); area.lineTo(plot.left(),plot.bottom()); area.closeSubpath();
    p.fillPath(area,QColor("#E6F4EC"));
    p.setPen(QPen(QColor("#00856A"),2.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.drawPath(line);
    p.setBrush(Qt::white);
    if(m_points.size()<=7) for(const auto &point:points) p.drawEllipse(point,3.5,3.5);
    if(highest==0) {
        p.setPen(QColor("#718078"));
        p.drawText(plot.adjusted(0,0,0,-16),Qt::AlignCenter,QStringLiteral("所选时段暂无已完成订单营收"));
    }
}

StateRing::StateRing(QWidget *parent):QWidget(parent)
{
    setMinimumSize(142,100);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("充电桩状态分布"));
}
void StateRing::setCounts(const QJsonObject &counts)
{
    m_counts=counts;
    QStringList descriptions;
    for(const auto &code:codes) descriptions.append(statusText(code)+QString::number(counts.value(code).toInt()));
    setAccessibleDescription(descriptions.join(QStringLiteral("，")));
    update();
}
void StateRing::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const qreal size=qMin(width()-28,height()-22);
    const QRectF ring((width()-size)/2,(height()-size)/2,size,size);
    QPen pen(QColor("#EDF1EC"),13); pen.setCapStyle(Qt::FlatCap); p.setPen(pen); p.drawEllipse(ring);
    const int total=m_counts.value("total").toInt();
    int start=90*16;
    if(total>0) for(const auto &code:codes) {
        const int span=qRound(360.0*16*m_counts.value(code).toInt()/total);
        pen.setColor(statusColor(code)); p.setPen(pen);
        if(span>0) p.drawArc(ring,start,-span);
        start-=span;
    }
    QFont number=font(); number.setPixelSize(30); number.setBold(true); p.setFont(number);
    p.setPen(QColor("#18352D"));
    p.drawText(ring.adjusted(0,-10,0,-10),Qt::AlignCenter,QString::number(total));
    QFont caption=font(); caption.setPixelSize(size<100 ? 11 : 12); p.setFont(caption); p.setPen(QColor("#718078"));
    p.drawText(QRectF(ring.left(),ring.center().y()+12,ring.width(),18),Qt::AlignCenter,QStringLiteral("充电桩总数"));
}
}
