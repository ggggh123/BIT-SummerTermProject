#include "ui/PulseChart.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace ev::ui {
PulseChart::PulseChart(bool compact,QWidget *parent):QWidget(parent),m_compact(compact)
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("功率轨迹，左右方向键回看，End 回到最新"));
    setToolTip(QStringLiteral("点击或拖动回看；左右方向键移动，Home / End 跳转首尾"));
}
QSize PulseChart::minimumSizeHint() const { return {m_compact?240:360,m_compact?180:230}; }
QString PulseChart::elapsed(double seconds)
{
    const qint64 s=qMax<qint64>(0,qRound64(seconds));
    return QStringLiteral("%1:%2").arg(s/60,2,10,QLatin1Char('0')).arg(s%60,2,10,QLatin1Char('0'));
}
void PulseChart::setSeries(QVector<PowerSeries> series,double duration,double minimumMaxKw)
{
    for(auto &s:series) {
        s.points.erase(std::remove_if(s.points.begin(),s.points.end(),[](const PowerPoint &p) {
            return !std::isfinite(p.seconds)||!std::isfinite(p.kw)||p.seconds<0||p.kw<0;
        }),s.points.end());
        std::sort(s.points.begin(),s.points.end(),[](const PowerPoint &a,const PowerPoint &b){return a.seconds<b.seconds;});
    }
    m_series=std::move(series);
    m_duration=qMax(1.0,duration);
    double maximum=qMax(1.0,minimumMaxKw);
    for(int i=0;i<m_series.size();++i) for(const auto &p:m_series[i].points) {
        double value=p.kw;
        for(int previous=0;previous<i;++previous) {
            const auto r=at(previous,p.seconds);
            if(r) value+=r->kw;
        }
        maximum=qMax(maximum,value);
    }
    const double step=maximum<=60?20:maximum<=100?25:std::pow(10,std::floor(std::log10(maximum)));
    m_max=std::ceil(maximum/step)*step;
    if(m_cursor>=0) m_cursor=qMin(m_cursor,m_duration);
    update();
}
void PulseChart::setMessage(const QString &message) { m_message=message;update(); }
void PulseChart::followLatest() { m_cursor=-1;update();emit cursorChanged(); }
void PulseChart::setCursorSeconds(double seconds)
{
    if(!std::isfinite(seconds))return;
    m_cursor=qBound(0.0,seconds,m_duration);update();emit cursorChanged();
}
double PulseChart::cursorSeconds() const {return m_cursor<0?m_duration:m_cursor;}
int PulseChart::sampleCount() const {int n=0;for(const auto &s:m_series)n+=s.points.size();return n;}
std::optional<PowerPoint> PulseChart::at(int i,double t) const
{
    if(i<0||i>=m_series.size()) return {};
    const auto &points=m_series[i].points;
    if(points.isEmpty() || t+0.001<points.first().seconds) return {};
    const auto next=std::lower_bound(points.begin(),points.end(),t,[](const PowerPoint &p,double x){return p.seconds<x;});
    if(next==points.end()) {
        if(t-points.last().seconds>15) return {};
        return points.last();
    }
    if(next==points.begin()||std::abs(next->seconds-t)<.001) return *next;
    const auto &previous=*(next-1);
    const double gap=next->seconds-previous.seconds;
    if(gap<=0||gap>120) return {};
    const double f=(t-previous.seconds)/gap;
    return PowerPoint{t,previous.kw+(next->kw-previous.kw)*f,previous.energy+(next->energy-previous.energy)*f};
}
std::optional<PowerPoint> PulseChart::reading(int index) const {return at(index,cursorSeconds());}
QRectF PulseChart::plotRect() const
{
    if(!m_preserveAspectRatio)
        return {m_compact?35.0:49.0,19.0,double(width()-(m_compact?47:69)),double(height()-(m_compact?49:53))};
    // 与原稿 SVG viewBox 的等比留白一致；不把纵横比例随容器拉伸。
    const double w=m_compact?340:820, h=m_compact?200:295;
    const double scale=qMin(width()/w,height()/h);
    const double dx=(width()-w*scale)/2,dy=(height()-h*scale)/2;
    return {dx+(m_compact?35:49)*scale,dy+19*scale,
            (w-(m_compact?47:69))*scale,(h-(m_compact?49:53))*scale};
}
void PulseChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    const QRectF r=plotRect();
    if(r.width()<=0||r.height()<=0) return;
    auto x=[&](double t){return r.left()+t/m_duration*r.width();};
    auto y=[&](double v){return r.bottom()-v/m_max*r.height();};
    QFont f(QStringLiteral("Noto Sans CJK SC"));f.setPixelSize(m_compact?11:12);p.setFont(f);
    const int divisions=m_max==60?3:4;
    for(int i=0;i<=divisions;++i) {
        const double v=m_max*i/divisions;
        p.setPen(QPen(QColor("#355161"),1));p.drawLine(QPointF(r.left(),y(v)),QPointF(r.right(),y(v)));
        p.setPen(QColor("#8aa9bc"));p.drawText(QRectF(0,y(v)-10,r.left()-8,20),Qt::AlignRight|Qt::AlignVCenter,QString::number(v,'g',4));
    }
    f.setPixelSize(m_compact?10:12);p.setFont(f);p.setPen(QColor("#8daabd"));
    for(int i=0;i<5;++i) {
        const double t=m_duration*i/4;
        const QRectF box(i==0?x(t):i==4?x(t)-80:x(t)-40,r.bottom()+8,80,22);
        p.drawText(box,(i==0?Qt::AlignLeft:i==4?Qt::AlignRight:Qt::AlignHCenter)|Qt::AlignVCenter,elapsed(t));
    }
    f.setPixelSize(10);p.setFont(f);p.drawText(QRectF(r.left(),0,40,16),Qt::AlignLeft,QStringLiteral("kW"));
    p.save();p.setClipRect(r.adjusted(-5,-5,5,5));
    // 对齐时刻后叠加；有缺失采样时分段，不把缺失值填成零。
    for(int index=0;index<m_series.size();++index) {
        const auto &s=m_series[index];
        QVector<QPointF> upper,lower;
        auto flush=[&] {
            if(upper.size()>1) {
                QPainterPath fill;fill.moveTo(upper.first());
                for(int n=1;n<upper.size();++n)fill.lineTo(upper[n]);
                for(int n=lower.size()-1;n>=0;--n)fill.lineTo(lower[n]);fill.closeSubpath();
                QColor area=s.color;area.setAlphaF(index==0?.17:.18);
                p.fillPath(fill,area);
                QPainterPath line;line.moveTo(upper.first());
                for(int n=1;n<upper.size();++n)line.lineTo(upper[n]);
                p.setPen(QPen(s.color,index==0?3:2.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(line);
            } else if(upper.size()==1) {p.setBrush(s.color);p.setPen(Qt::NoPen);p.drawEllipse(upper.first(),3,3);}
            upper.clear();lower.clear();
        };
        double last=-1;
        for(const auto &point:s.points) {
            double base=0;bool valid=true;
            for(int j=0;j<index;++j){const auto other=at(j,point.seconds);if(!other){valid=false;break;}base+=other->kw;}
            if(!valid|| (last>=0&&point.seconds-last>120)) flush();
            if(valid){upper.append({x(point.seconds),y(base+point.kw)});lower.append({x(point.seconds),y(base)});}
            last=point.seconds;
        }
        flush();
    }
    if(sampleCount()>0) {
        const double cx=x(cursorSeconds());p.setPen(QPen(QColor("#b4cbd5"),1,Qt::DashLine));
        p.drawLine(QPointF(cx,r.top()),QPointF(cx,r.bottom()));
        double total=0;
        for(int i=0;i<m_series.size();++i){const auto v=reading(i);if(!v)break;total+=v->kw;
            p.setBrush(m_series[i].color.lighter(115));p.setPen(QPen(QColor("#132f40"),2));p.drawEllipse(QPointF(cx,y(total)),5,5);}
    }
    p.restore();
    if(!m_message.isEmpty()||sampleCount()==0) {
        p.setPen(QColor("#97adbc"));f.setPixelSize(m_compact?12:14);p.setFont(f);
        p.drawText(r.adjusted(15,10,-15,-10),Qt::AlignCenter|Qt::TextWordWrap,
                   m_message.isEmpty()?QStringLiteral("等待功率采样\n收到设备数据后显示曲线"):m_message);
    }
    if(hasFocus()){p.setPen(QPen(QColor("#72ddc3"),1));p.setBrush(Qt::NoBrush);p.drawRect(rect().adjusted(1,1,-2,-2));}
}
void PulseChart::choose(double px)
{
    if(sampleCount()==0)return;
    m_cursor=qBound(0.0,(px-plotRect().left())/plotRect().width()*m_duration,m_duration);
    update();emit cursorChanged();
}
void PulseChart::mousePressEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton)choose(e->position().x());}
void PulseChart::mouseMoveEvent(QMouseEvent *e){if(e->buttons()&Qt::LeftButton)choose(e->position().x());}
void PulseChart::keyPressEvent(QKeyEvent *e)
{
    if(e->key()==Qt::Key_End){followLatest();return;}
    if(e->key()==Qt::Key_Home)m_cursor=0;
    else if(e->key()==Qt::Key_Left||e->key()==Qt::Key_Right)m_cursor=qBound(0.0,cursorSeconds()+(e->key()==Qt::Key_Left?-1:1)*m_duration/100,m_duration);
    else {QWidget::keyPressEvent(e);return;}
    update();emit cursorChanged();
}
} // namespace ev::ui
