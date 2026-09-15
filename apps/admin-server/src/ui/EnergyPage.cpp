#include "ui/EnergyPage.h"
#include "ui/PulseChart.h"
#include "ui/AdminVisuals.h"
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimeZone>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QLabel *label(const QString &text,const QString &style={}) {
    auto *l=new QLabel(text);l->setTextFormat(Qt::PlainText);l->setStyleSheet(style);
    return l;
}
QString timeText(const QString &iso) {return QDateTime::fromString(iso,Qt::ISODateWithMs).toOffsetFromUtc(8*3600).toString("HH:mm:ss");}
}

EnergyPage::EnergyPage(QWidget *parent):QWidget(parent)
{
    setObjectName("adminEnergyPage");
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
    auto *heading=new QHBoxLayout;
    auto *headingText=new QVBoxLayout;headingText->setSpacing(5);
    headingText->addWidget(label(QStringLiteral("能量正在怎样流动"),"font-size:22px;font-weight:600;color:#f0f6f8;"));
    m_sampleNote=label(QStringLiteral("等待设备采样"),"font-size:12px;color:#9eb6c6;");
    headingText->addWidget(m_sampleNote);
    heading->addLayout(headingText,1);
    m_station=new QComboBox;m_station->setObjectName("energyStationFilter");
    m_station->setAccessibleName(QStringLiteral("能量视图所属站点"));m_station->setMaximumWidth(210);
    m_station->setMinimumWidth(200);
    heading->addWidget(m_station);
    layout->addLayout(heading);layout->addSpacing(18);
    auto *work=new QHBoxLayout;work->setSpacing(24);
    auto *main=new QWidget;auto *mainLayout=new QVBoxLayout(main);
    mainLayout->setContentsMargins(0,0,0,0);mainLayout->setSpacing(0);
    auto *totalRow=new QHBoxLayout;
    m_total=label(QStringLiteral("—"),"font-size:49px;font-weight:500;color:#c6f7e9;");m_total->setObjectName("energyTotalPower");
    m_total->setFixedHeight(54);
    auto *totalUnit = label(QStringLiteral("kW / 所选采样点合计"),"font-size:14px;color:#95b5c4;");
    totalUnit->setFixedHeight(30);
    totalRow->addWidget(m_total);totalRow->addWidget(totalUnit,0,Qt::AlignBottom);
    totalRow->addStretch();m_legend=new QLabel;m_legend->setTextFormat(Qt::RichText);totalRow->addWidget(m_legend);
    mainLayout->addLayout(totalRow);mainLayout->addSpacing(8);
    m_chart=new ev::ui::PulseChart(false);m_chart->setObjectName("adminPowerChart");m_chart->setFixedHeight(300);
    mainLayout->addWidget(m_chart);
    mainLayout->addWidget(label(QStringLiteral("青绿与暖橙为所选设备的功率叠加；虚线为回看游标。缺失采样保留断点。"),"font-size:10px;color:#89a8ba;"));
    mainLayout->addSpacing(14);
    auto *separator=new QFrame;separator->setFixedHeight(1);separator->setStyleSheet("background:#304855;");
    mainLayout->addWidget(separator);mainLayout->addSpacing(14);
    auto *devices=new QWidget;
    m_deviceGrid=new QGridLayout(devices);m_deviceGrid->setContentsMargins(0,0,0,0);m_deviceGrid->setSpacing(11);
    m_deviceGrid->setAlignment(Qt::AlignTop);
    auto *deviceScroll=new QScrollArea;deviceScroll->setObjectName("energyDevicesViewport");deviceScroll->setWidgetResizable(true);
    deviceScroll->setFrameShape(QFrame::NoFrame);deviceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    deviceScroll->setWidget(devices);deviceScroll->setFixedHeight(126);
    mainLayout->addWidget(deviceScroll);
    mainLayout->addSpacing(10);
    auto *context=label(QStringLiteral("标签为当前设备状态；功率条为最后一条实际采样，不代表额定功率。"),"font-size:11px;color:#9bb5c5;");
    context->setWordWrap(true);mainLayout->addWidget(context);
    mainLayout->addStretch(1);
    work->addWidget(main,1);
    auto *aside=new QFrame;aside->setObjectName("energySelectedDevice");aside->setFixedWidth(244);
    aside->setStyleSheet("QFrame#energySelectedDevice{background:#193444;border:1px solid #2e5364;border-radius:10px;}");
    auto *side=new QVBoxLayout(aside);side->setContentsMargins(20,20,20,20);side->setSpacing(0);
    side->addWidget(label(QStringLiteral("选中设备 / 样本读数"),"font-size:12px;color:#9bb7c7;"));side->addSpacing(14);
    m_deviceName=label(QStringLiteral("—"),"font-size:34px;font-weight:600;color:#eaf3f6;");m_deviceName->setObjectName("energySelectedCode");side->addWidget(m_deviceName);
    side->addSpacing(5);m_deviceStatus=label({},"font-size:12px;color:#8be4ce;");m_deviceStatus->setWordWrap(true);side->addWidget(m_deviceStatus);
    auto reading=[&](const QString &caption,QLabel **value,const char *name,const QString &unit){
        side->addSpacing(18);side->addWidget(label(caption,"font-size:12px;color:#98b8ca;"));side->addSpacing(7);
        *value=label(QStringLiteral("—"),"font-size:30px;color:#e4f8f2;");(*value)->setObjectName(name);
        (*value)->setFixedHeight(45);
        auto *valueRow = new QHBoxLayout;valueRow->setSpacing(6);
        auto *unitLabel=label(unit,"font-size:13px;color:#93b5c8;");unitLabel->setFixedHeight(28);
        valueRow->addWidget(*value);valueRow->addWidget(unitLabel,0,Qt::AlignBottom);valueRow->addStretch();
        side->addLayout(valueRow);side->addSpacing(18);
        auto *line=new QFrame;line->setFixedHeight(1);line->setStyleSheet("background:#345060;");side->addWidget(line);
    };
    reading(QStringLiteral("所选时刻功率"),&m_power,"energySelectedPower",QStringLiteral("kW"));
    reading(QStringLiteral("窗口积分电量 / 非账单"),&m_energy,"energyWindowEnergy",QStringLiteral("kWh"));
    side->addSpacing(19);m_readingNote=label({},"font-size:11px;color:#94b1c3;");m_readingNote->setWordWrap(true);side->addWidget(m_readingNote);
    side->addStretch();m_latest=new QPushButton(QStringLiteral("回到最新采样"));m_latest->setObjectName("energyFollowLatest");side->addWidget(m_latest);side->addSpacing(12);
    m_detail=new QPushButton(QStringLiteral("查看设备档案"));m_detail->setProperty("role","primary");m_detail->setMinimumHeight(34);side->addWidget(m_detail);
    work->addWidget(aside);layout->addLayout(work,1);
    connect(m_chart,&ev::ui::PulseChart::cursorChanged,this,&EnergyPage::renderReading);
    connect(m_latest,&QPushButton::clicked,m_chart,&ev::ui::PulseChart::followLatest);
    connect(m_station,&QComboBox::currentIndexChanged,this,[this]{m_selected=0;m_chart->followLatest();emit stationChanged();});
    connect(m_detail,&QPushButton::clicked,this,[this]{
        for(const auto &v:m_devices){const auto d=v.toObject();if(d.value("chargerId").toInteger()!=m_selected)continue;
            QMessageBox box(QMessageBox::Information,QStringLiteral("设备档案"),
                QStringLiteral("%1\n%2\n类型：%3\n额定功率：%4 kW\n状态：%5\n\n曲线来自设备采样；额定功率不是实时功率。")
                .arg(d.value("code").toString(),m_station->currentText(),AdminVisuals::statusText(d.value("type").toString()))
                .arg(d.value("ratedPowerKw").toDouble()).arg(AdminVisuals::statusText(d.value("status").toString())),QMessageBox::Ok,this);
            box.setTextFormat(Qt::PlainText);box.button(QMessageBox::Ok)->setText(QStringLiteral("关闭"));box.exec();break;
        }
    });
}
qint64 EnergyPage::stationId() const {return m_station->currentData().toLongLong();}
void EnergyPage::setData(const QJsonObject &data)
{
    const QSignalBlocker blocker(m_station);
    const qint64 id=data.value("stationId").toInteger();
    const auto stations=data.value("stations").toArray();
    m_station->clear();for(const auto &v:stations){const auto s=v.toObject();m_station->addItem(s.value("name").toString(),s.value("stationId").toInteger());}
    m_station->setCurrentIndex(m_station->findData(id));
    const auto incoming=data.value("chargers").toArray();
    bool changed=incoming.size()!=m_devices.size();
    if(!changed)for(int i=0;i<incoming.size();++i)if(incoming[i].toObject().value("chargerId")!=m_devices[i].toObject().value("chargerId")){changed=true;break;}
    m_devices=incoming;m_latestAt=data.value("latestAt").toString();
    if(changed){
        qDeleteAll(m_deviceButtons);m_deviceButtons.clear();
        for(int i=0;i<m_devices.size();++i){auto *button=new QPushButton;button->setCheckable(true);button->setAutoExclusive(false);
            button->setFixedHeight(56);button->setMinimumWidth(80);button->setObjectName(QStringLiteral("energyDevice%1").arg(i));
            button->setStyleSheet("QPushButton{background:transparent;border:1px solid #2a4555;border-radius:6px;padding:0;min-height:54px;max-height:54px;}QPushButton:checked{border-color:#7acdbb;background:#1b3d4d;}QPushButton:focus{border:2px solid #a2f3df;}");
            auto *bl=new QVBoxLayout(button);bl->setContentsMargins(10,7,10,7);bl->setSpacing(6);
            auto *caption=label({},"font-size:11px;color:#b7ccd8;");caption->setObjectName("energyDeviceCaption");caption->setAttribute(Qt::WA_TransparentForMouseEvents);bl->addWidget(caption);
            auto *bar=new QProgressBar;bar->setRange(0,1000);bar->setTextVisible(false);bar->setFixedHeight(6);bar->setAttribute(Qt::WA_TransparentForMouseEvents);
            bar->setStyleSheet("QProgressBar{border:0;background:#223d4d;border-radius:3px;}QProgressBar::chunk{background:#75ddc4;}");bl->addWidget(bar);
            m_deviceGrid->addWidget(button,i/4,i%4);m_deviceButtons.append(button);
            m_deviceGrid->setRowMinimumHeight(i/4,56);
            connect(button,&QPushButton::clicked,this,[this,i]{selectDevice(m_devices.at(i).toObject().value("chargerId").toInteger());});
        }
    }
    bool found=false;for(const auto &v:m_devices)if(v.toObject().value("chargerId").toInteger()==m_selected)found=true;
    if(!found)m_selected=m_devices.isEmpty()?0:m_devices.first().toObject().value("chargerId").toInteger();
    for(int i=0;i<m_deviceButtons.size();++i){const auto d=m_devices[i].toObject();auto *button=m_deviceButtons[i];
        const QString caption=d.value("code").toString()+QStringLiteral("   ")+AdminVisuals::statusText(d.value("status").toString());
        button->findChild<QLabel *>("energyDeviceCaption")->setText(caption);button->setAccessibleName(caption);
        button->setChecked(d.value("chargerId").toInteger()==m_selected);
        const auto samples=d.value("samples").toArray();const double rated=d.value("ratedPowerKw").toDouble();
        const double power=samples.isEmpty()?0:samples.last().toObject().value("powerKw").toDouble();
        button->findChild<QProgressBar *>()->setValue(rated>0?qBound(0,qRound(power/rated*1000),1000):0);
        button->setToolTip(samples.isEmpty()?QStringLiteral("未收到采样"):QStringLiteral("最后采样 %1：%2 kW / 额定 %3 kW").arg(timeText(samples.last().toObject().value("recordedAt").toString())).arg(power).arg(rated));
    }
    renderChart();
}
void EnergyPage::selectDevice(qint64 id){m_selected=id;for(int i=0;i<m_deviceButtons.size();++i)m_deviceButtons[i]->setChecked(m_devices[i].toObject().value("chargerId").toInteger()==id);renderChart();}
void EnergyPage::renderChart()
{
    const std::optional<double> pinnedTime = m_chart->followingLatest() ? std::nullopt
        : std::optional<double>(m_startMs+m_chart->cursorSeconds()*1000);
    m_seriesIds.clear();
    // 以选中设备和另一台有采样的设备对照，避免数十条曲线挤在同一视图。
    if(m_selected>0)m_seriesIds.append(m_selected);
    for(const auto &v:m_devices){const auto d=v.toObject();if(d.value("chargerId").toInteger()!=m_selected&&!d.value("samples").toArray().isEmpty()){m_seriesIds.append(d.value("chargerId").toInteger());break;}}
    QVector<ev::ui::PowerSeries> series;double start=0,end=0;
    const auto candidates=m_seriesIds;
    m_seriesIds.clear();
    for(auto id:candidates)for(const auto &v:m_devices){const auto d=v.toObject();if(d.value("chargerId").toInteger()!=id || d.value("samples").toArray().isEmpty())continue;
        m_seriesIds.append(id);
        const auto samples=d.value("samples").toArray();if(samples.isEmpty())continue;
        const double a=QDateTime::fromString(samples.first().toObject().value("recordedAt").toString(),Qt::ISODateWithMs).toMSecsSinceEpoch();
        const double b=QDateTime::fromString(samples.last().toObject().value("recordedAt").toString(),Qt::ISODateWithMs).toMSecsSinceEpoch();
        start=start==0?a:qMin(start,a);end=qMax(end,b);
    }
    m_startMs=start;QStringList legend;
    for(auto id:m_seriesIds)for(const auto &v:m_devices){const auto d=v.toObject();if(d.value("chargerId").toInteger()!=id)continue;
        ev::ui::PowerSeries s;s.id=QString::number(id);s.name=d.value("code").toString();s.color=QColor(series.isEmpty()?"#72e0c7":"#f0af8e");
        double previousTime=-1,previousPower=0,energy=0;
        for(const auto &value:d.value("samples").toArray()){const auto p=value.toObject();const double t=(QDateTime::fromString(p.value("recordedAt").toString(),Qt::ISODateWithMs).toMSecsSinceEpoch()-start)/1000;
            const double power=p.value("powerKw").toDouble();if(previousTime>=0&&t-previousTime<=120)energy+=(previousPower+power)/2*(t-previousTime)/3600;
            s.points.append({t,power,energy});previousTime=t;previousPower=power;
        }
        legend.append(QStringLiteral("<span style='color:%1'>%2</span>").arg(s.color.name(),s.name.toHtmlEscaped()));series.append(s);
    }
    m_chart->setSeries(series,end>start?(end-start)/1000:1800,100);m_legend->setText(legend.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    // 窗口滑动时锁住回看的绝对时刻，不把历史游标悄悄平移到新的数据上。
    if(pinnedTime && start>0) m_chart->setCursorSeconds((*pinnedTime-start)/1000);
    renderReading();
}
void EnergyPage::renderReading()
{
    std::optional<ev::ui::PowerPoint> selected;
    double total=0;bool all=!m_seriesIds.isEmpty();
    for(int i=0;i<m_seriesIds.size();++i){const auto r=m_chart->reading(i);if(!r)all=false;else total+=r->kw;if(m_seriesIds[i]==m_selected)selected=r;}
    m_total->setText(all?QString::number(total,'f',1):QStringLiteral("—"));
    m_power->setText(selected?QString::number(selected->kw,'f',1):QStringLiteral("—"));
    m_energy->setText(selected?QString::number(selected->energy,'f',3):QStringLiteral("—"));
    m_deviceName->setText(QStringLiteral("—"));m_deviceStatus->clear();
    m_detail->setEnabled(m_selected>0);
    for(const auto &v:m_devices){const auto d=v.toObject();if(d.value("chargerId").toInteger()!=m_selected)continue;
        m_deviceName->setText(d.value("code").toString());m_deviceStatus->setText(QStringLiteral("%1 · 额定 %2 kW").arg(AdminVisuals::statusText(d.value("status").toString())).arg(d.value("ratedPowerKw").toDouble()));}
    const auto latest=QDateTime::fromString(m_latestAt,Qt::ISODateWithMs);
    const bool stale=latest.isValid()&&latest.secsTo(QDateTime::currentDateTimeUtc())>15;
    const QString time=QDateTime::fromMSecsSinceEpoch(qRound64(m_startMs+m_chart->cursorSeconds()*1000),QTimeZone(0)).toOffsetFromUtc(8*3600).toString("HH:mm:ss");
    m_sampleNote->setText(!latest.isValid()?QStringLiteral("暂无设备采样 · 收到数据后显示曲线")
        :QStringLiteral("最近采样窗口 · 游标 %1 · %2").arg(time,stale?QStringLiteral("历史数据 / 超过 15 秒未更新"):QStringLiteral("设备采样")));
    m_readingNote->setText(selected?QStringLiteral("读数来自图中的同一组采样。\n窗口电量为有效线段的积分，不用于订单结算。")
                         :QStringLiteral("所选时刻没有有效采样。\n缺失数据不会用零值代替。"));
}
