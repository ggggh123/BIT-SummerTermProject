#include "ui/OperationsPages.h"
#include "ui/AdminVisuals.h"
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
class DeviceCodeLabel final : public QLabel {
public:
    explicit DeviceCodeLabel(const QString &fullText):QLabel(fullText),m_fullText(fullText)
    {
        setObjectName("fleetDeviceCode");
        setTextFormat(Qt::PlainText);
        setAccessibleName(fullText);
        setToolTip(fullText);
        setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    }
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        refreshText();
    }
    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if(event->type()==QEvent::FontChange||event->type()==QEvent::ContentsRectChange)
            refreshText();
    }
private:
    void refreshText()
    {
        // 使用布局完成后的自身宽度；窗口缩放不依赖下一次设备状态变化。
        setText(fontMetrics().elidedText(m_fullText,Qt::ElideRight,contentsRect().width()));
    }
    QString m_fullText;
};

const QStringList stateCodes={"idle","reserved","charging","fault","restarting"};
QColor stateColor(const QString &state)
{
    if(state=="charging") return QColor("#72e0c7");
    if(state=="fault") return QColor("#f0af8e");
    if(state=="reserved"||state=="restarting") return QColor("#e9bc87");
    return QColor("#97adbc");
}
QString stateText(const QString &state)
{
    const QString text=AdminVisuals::statusText(state);
    return text.isEmpty()?state:text;
}
QLabel *text(const QString &value,const char *role="secondary")
{
    auto *label=new QLabel(value); label->setProperty("role",role);
    label->setTextFormat(Qt::PlainText);
    return label;
}
void colorText(QLabel *label,const QColor &color,int size=13)
{
    label->setStyleSheet(QStringLiteral("color:%1; font-size:%2px; background:transparent; border:0;")
        .arg(color.name()).arg(size));
}
QFrame *panel(const char *name)
{
    auto *frame=new QFrame; frame->setObjectName(QString::fromLatin1(name));
    frame->setProperty("role","card");
    auto *layout=new QVBoxLayout(frame);
    layout->setContentsMargins(20,18,20,18); layout->setSpacing(12);
    return frame;
}
QTableWidget *table(const QStringList &headers,const char *name)
{
    auto *result=new QTableWidget(0,headers.size());
    result->setObjectName(QString::fromLatin1(name));
    result->setHorizontalHeaderLabels(headers);
    result->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    result->verticalHeader()->hide();
    result->verticalHeader()->setDefaultSectionSize(38);
    result->setShowGrid(false);
    result->setWordWrap(false);
    result->setTextElideMode(Qt::ElideRight);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    result->setItemDelegate(new AdminVisuals::StatusDelegate(result));
    return result;
}
void fill(QTableWidget *table,const QList<QStringList> &rows)
{
    const QSignalBlocker blocker(table);
    table->setRowCount(rows.size());
    for(int row=0;row<rows.size();++row) for(int col=0;col<rows[row].size();++col) {
        auto *item=table->item(row,col);
        if(!item) { item=new QTableWidgetItem; table->setItem(row,col,item); }
        const QString value=rows[row][col];
        item->setText(value); item->setToolTip(value);
        const auto display=AdminVisuals::statusText(value);
        const bool status=table->horizontalHeaderItem(col)->text().contains(QStringLiteral("状态"))||table->horizontalHeaderItem(col)->text().contains(QStringLiteral("类型"));
        item->setData(Qt::AccessibleTextRole,status?display:QString());
        item->setData(Qt::UserRole+1,status&&!display.isEmpty());
    }
}
QString clockText(const QString &iso)
{
    const auto dt=QDateTime::fromString(iso,Qt::ISODateWithMs);
    return dt.isValid()?dt.toOffsetFromUtc(8*3600).toString("HH:mm:ss"):QStringLiteral("—");
}
QLabel *largeText(const QString &value,const char *name,int size=30)
{
    auto *label=text(value);
    label->setObjectName(QString::fromLatin1(name));
    colorText(label,QColor("#eaf3f6"),size);
    return label;
}
}

class StateStrip final : public QWidget {
public:
    explicit StateStrip(QWidget *parent=nullptr):QWidget(parent){ setFixedHeight(7); }
    void setCounts(const QJsonObject &counts) { m_counts=counts; update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(),QColor("#2c4251"));
        const int total=m_counts.value("total").toInt();
        if(total<=0) return;
        qreal x=0;
        for(const QString &code:stateCodes+QStringList{"unknown"}) {
            const qreal w=width()*m_counts.value(code).toDouble()/total;
            p.fillRect(QRectF(x,0,w,height()),stateColor(code)); x+=w;
        }
    }
private:
    QJsonObject m_counts;
};

FleetStatusPage::FleetStatusPage(QWidget *parent):QWidget(parent)
{
    setObjectName("fleetStatusPage");
    setStyleSheet(QStringLiteral(R"QSS(
QPushButton[role="fleetFilter"] { min-height:44px; max-height:44px; padding:6px 10px; color:#b8cdd8; }
QPushButton[role="fleetFilter"]:checked { background:#204654; border-color:#72ddc3; color:#a5f1df; }
QPushButton[role="deviceTile"] { min-height:54px; max-height:54px; padding:0; background:#132c3c; border:1px solid #355161; border-radius:7px; }
QPushButton[role="deviceTile"]:checked { border:2px solid #72ddc3; background:#204654; }
QPushButton[role="deviceTile"]:hover { background:#234858; }
QFrame[role="stationGroup"] { background:#132b3b; border:1px solid #2c4251; border-radius:9px; }
QLabel { border:0; background:transparent; }
QPushButton[role="primary"] { color:#102d35; }
)QSS"));
    auto *layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(15);
    auto *top=new QHBoxLayout;
    auto *numbers=new QHBoxLayout;
    m_total=largeText("—","fleetTotal",49);
    numbers->addWidget(m_total);
    auto *caption=text(QStringLiteral("台设备\n当前站点范围"));
    numbers->addWidget(caption); numbers->addStretch();
    top->addLayout(numbers,1);
    m_station=new QComboBox;
    m_station->setObjectName("fleetStationFilter");
    m_station->setAccessibleName(QStringLiteral("按站点筛选设备阵列"));
    m_station->setMinimumWidth(220); m_station->setMaximumWidth(280);
    m_station->addItem(QStringLiteral("全部站点"),0);
    top->addWidget(m_station);
    auto *refresh=new QPushButton(QStringLiteral("刷新快照"));
    refresh->setObjectName("fleetRefreshButton"); top->addWidget(refresh);
    layout->addLayout(top);
    auto *filters=new QHBoxLayout; filters->setSpacing(9);
    for(const auto &code:QStringList{""}+stateCodes) {
        auto *button=new QPushButton(code.isEmpty()?QStringLiteral("全部\n—"):stateText(code)+"\n—");
        button->setObjectName("fleetFilter_"+(code.isEmpty()?QString("all"):code));
        button->setProperty("role","fleetFilter"); button->setCheckable(true);
        button->setAccessibleName(code.isEmpty()?QStringLiteral("全部状态"):stateText(code));
        m_filters.insert(code,button); filters->addWidget(button,1);
        connect(button,&QPushButton::clicked,this,[this,code] {m_status=code;render();});
    }
    m_filters[""]->setChecked(true);
    layout->addLayout(filters);
    m_strip=new StateStrip(this); layout->addWidget(m_strip);
    auto *body=new QHBoxLayout; body->setSpacing(24);
    auto *left=new QVBoxLayout; left->setSpacing(12);
    auto *heading=new QHBoxLayout;
    heading->addWidget(text(QStringLiteral("站点设备阵列"),"sectionTitle"));
    heading->addStretch();
    auto *mode=new QPushButton(QStringLiteral("列表视图"));
    mode->setObjectName("fleetViewToggle"); heading->addWidget(mode);
    left->addLayout(heading);
    m_views=new QStackedWidget;
    m_array=new QScrollArea;
    m_array->setObjectName("fleetArrayViewport");
    m_array->setWidgetResizable(true); m_array->setFrameShape(QFrame::NoFrame);
    m_array->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *arrayBody=new QWidget;
    m_stationGrid=new QGridLayout(arrayBody);
    m_stationGrid->setContentsMargins(0,0,8,0);
    m_stationGrid->setSpacing(12); m_stationGrid->setAlignment(Qt::AlignTop);
    m_stationGrid->setSizeConstraint(QLayout::SetMinimumSize);
    m_array->setWidget(arrayBody);
    m_views->addWidget(m_array);
    m_table=table({QStringLiteral("桩ID"),QStringLiteral("编号"),QStringLiteral("所属电站"),
        QStringLiteral("类型"),QStringLiteral("额定功率(kW)"),QStringLiteral("状态"),
        QStringLiteral("累计次数"),QStringLiteral("累计时长(s)")},"pileStatusDetailTable");
    m_views->addWidget(m_table);
    left->addWidget(m_views,1);
    m_empty=text(QStringLiteral("正在读取站点与设备…"));
    m_empty->setObjectName("fleetEmptyState"); left->addWidget(m_empty);
    body->addLayout(left,1);
    auto *inspector=panel("fleetInspector"); inspector->setFixedWidth(252);
    auto *info=qobject_cast<QVBoxLayout *>(inspector->layout());
    info->setSpacing(8);
    info->addWidget(text(QStringLiteral("选中设备 / 处置"),"secondary"));
    m_code=largeText(QStringLiteral("未选择"),"fleetSelectedCode",34); info->addWidget(m_code);
    m_stationName=text(QStringLiteral("点击阵列或列表中的设备")); m_stationName->setWordWrap(true); info->addWidget(m_stationName);
    m_state=text("—"); m_state->setObjectName("fleetSelectedState"); info->addWidget(m_state);
    info->addSpacing(12);
    info->addWidget(text(QStringLiteral("额定功率")));
    m_rated=largeText("—","fleetSelectedRated",27); info->addWidget(m_rated);
    auto *totals=new QGridLayout;
    totals->addWidget(text(QStringLiteral("累计次数")),0,0);
    totals->addWidget(text(QStringLiteral("累计时长")),0,1);
    m_count=largeText("—","fleetSelectedCount",23); totals->addWidget(m_count,1,0);
    m_duration=largeText("—","fleetSelectedDuration",23); totals->addWidget(m_duration,1,1);
    info->addLayout(totals);
    info->addStretch();
    auto *note=text(QStringLiteral("仅故障设备可重启。设备状态以服务端回包和后续快照为准。"));
    note->setWordWrap(true); info->addWidget(note);
    m_restart=new QPushButton(QStringLiteral("重启所选故障桩"));
    m_restart->setObjectName("fleetRestartButton"); m_restart->setProperty("role","danger");
    m_restart->setEnabled(false); info->addWidget(m_restart);
    auto *logs=new QPushButton(QStringLiteral("查看请求日志")); logs->setObjectName("fleetLogsButton"); info->addWidget(logs);
    body->addWidget(inspector);
    layout->addLayout(body,1);
    m_meta=text(QStringLiteral("等待首次数据库快照")); m_meta->setWordWrap(true); m_meta->setObjectName("fleetReadStatus"); layout->addWidget(m_meta);
    connect(refresh,&QPushButton::clicked,this,&FleetStatusPage::refreshRequested);
    connect(m_station,&QComboBox::currentIndexChanged,this,[this]{render();});
    connect(mode,&QPushButton::clicked,this,[this,mode]{
        m_views->setCurrentIndex(1-m_views->currentIndex());
        mode->setText(m_views->currentIndex()==0?QStringLiteral("列表视图"):QStringLiteral("阵列视图"));
    });
    connect(m_table,&QTableWidget::itemSelectionChanged,this,[this]{
        const int row=m_table->currentRow();
        m_selected=row>=0&&m_table->item(row,0)?m_table->item(row,0)->text().toInt():0;
        updateSelection();
    });
    connect(m_restart,&QPushButton::clicked,this,[this]{if(m_restart->isEnabled())emit restartRequested(m_selected);});
    connect(logs,&QPushButton::clicked,this,&FleetStatusPage::logsRequested);
}

void FleetStatusPage::setData(const QJsonObject &data)
{
    const bool changed=data.value("stations")!=m_data.value("stations")||data.value("chargers")!=m_data.value("chargers");
    m_data=data; m_fresh=true;
    colorText(m_meta,QColor("#97adbc"),12);
    m_meta->setText(QStringLiteral("数据库快照 %1  /  按站点 ID 分组  /  状态变化每秒刷新；额定功率不是瞬时采样")
        .arg(clockText(data.value("readAt").toString())));
    if(changed) {
        const QSignalBlocker blocker(m_station);
        const int previous=m_station->currentData().toInt();
        m_station->clear(); m_station->addItem(QStringLiteral("全部站点"),0);
        for(const auto &value:data.value("stations").toArray()) {
            const auto station=value.toObject();
            m_station->addItem(station.value("name").toString()+QStringLiteral(" · #%1").arg(station.value("id").toInt()),station.value("id").toInt());
        }
        const int index=m_station->findData(previous); m_station->setCurrentIndex(index<0?0:index);
        render();
    } else updateSelection();
}
void FleetStatusPage::setReadError(const QString &message)
{
    m_fresh=false; colorText(m_meta,QColor("#f0af8e"),12);
    m_meta->setText(QStringLiteral("读取失败，停止设备操作；保留的旧快照仅供参考 · ")+message);
    if(m_data.isEmpty())m_empty->setText(QStringLiteral("无法读取设备，请刷新重试。"));
    updateSelection();
}
void FleetStatusPage::setRestartPending(bool pending)
{
    if(m_pending&&!pending) {
        m_fresh=false;
        m_meta->setText(QStringLiteral("操作已返回，等待最新设备快照；暂不重复提交。"));
    }
    m_pending=pending; updateSelection();
}
void FleetStatusPage::selectCharger(int id)
{
    const bool filtersChanged=!m_status.isEmpty()||m_station->currentData().toInt()!=0;
    m_selected=id; m_status.clear();
    const QSignalBlocker blocker(m_station); m_station->setCurrentIndex(0);
    if(filtersChanged)render(); else updateSelection();
    m_views->setCurrentIndex(0);
    findChild<QPushButton *>("fleetViewToggle")->setText(QStringLiteral("列表视图"));
    // 页签切换和筛选后的布局尚未稳定；在下一轮事件中定位，避免按旧几何滚动。
    QTimer::singleShot(0,this,[this,id]{
        if(id!=m_selected)return;
        m_stationGrid->activate();
        if(auto *button=m_tiles.value(id))m_array->ensureWidgetVisible(button);
    });
}
void FleetStatusPage::render()
{
    const int scroll=m_array->verticalScrollBar()->value();
    m_tiles.clear();
    while(auto *item=m_stationGrid->takeAt(0)){delete item->widget();delete item;}
    const int stationId=m_station->currentData().toInt();
    const bool loaded=m_data.contains("chargers");
    const auto devices=m_data.value("chargers").toArray();
    QJsonObject counts;
    QHash<int,QList<QJsonObject>> groups;
    QList<QStringList> rows;
    bool selectionVisible=false;
    for(const auto &value:devices) {
        const auto device=value.toObject();
        if(stationId>0&&device.value("stationId").toInt()!=stationId)continue;
        const QString code=device.value("status").toString();
        counts["total"]=counts.value("total").toInt()+1;
        const QString category=stateCodes.contains(code)?code:QString("unknown");
        counts[category]=counts.value(category).toInt()+1;
        if(!m_status.isEmpty()&&m_status!=code)continue;
        groups[device.value("stationId").toInt()].append(device);
        const int id=device.value("id").toInt();
        selectionVisible|=id==m_selected;
        rows.append({QString::number(id),device.value("code").toString(),device.value("stationName").toString(),
            device.value("type").toString(),QString::number(device.value("ratedPowerKw").toDouble(),'g',5),code,
            QString::number(device.value("chargeCount").toInteger()),QString::number(device.value("totalDurationSec").toInteger())});
    }
    if(!selectionVisible)m_selected=0;
    m_total->setText(loaded?QString::number(counts.value("total").toInt()):QStringLiteral("—"));
    for(auto it=m_filters.begin();it!=m_filters.end();++it) {
        it.value()->setChecked(it.key()==m_status);
        it.value()->setText((it.key().isEmpty()?QStringLiteral("全部"):stateText(it.key()))+"\n"+
            (loaded?QString::number(counts.value(it.key().isEmpty()?"total":it.key()).toInt()):QStringLiteral("—")));
    }
    m_strip->setCounts(counts);
    int groupIndex=0;
    for(const auto &value:m_data.value("stations").toArray()) {
        const auto station=value.toObject(); const int id=station.value("id").toInt();
        if(stationId>0&&stationId!=id)continue;
        if(!m_status.isEmpty()&&!groups.contains(id))continue;
        auto *group=new QFrame;
        group->setObjectName(QString("fleetStation%1").arg(id)); group->setProperty("role","stationGroup");
        auto *grid=new QGridLayout(group); grid->setContentsMargins(12,10,12,12); grid->setSpacing(8);
        grid->setSizeConstraint(QLayout::SetMinimumSize);
        grid->setRowMinimumHeight(0,26);
        auto *name=text(station.value("name").toString(),"sectionTitle");
        name->setStyleSheet("font-size:14px;color:#d8e6ed;border:0;background:transparent;");
        name->setWordWrap(true); name->setToolTip(station.value("address").toString());
        grid->addWidget(name,0,0,1,3);
        auto *count=text(QStringLiteral("%1 台").arg(groups.value(id).size())); grid->addWidget(count,0,3,Qt::AlignRight);
        int index=0;
        for(const auto &device:groups.value(id)) {
            const int chargerId=device.value("id").toInt();
            auto *tile=new QPushButton; tile->setProperty("role","deviceTile");
            tile->setObjectName(QString("fleetDevice%1").arg(chargerId)); tile->setCheckable(true); tile->setMinimumWidth(58);
            auto *parts=new QVBoxLayout(tile); parts->setContentsMargins(8,4,8,4); parts->setSpacing(1);
            auto *code=new DeviceCodeLabel(device.value("code").toString()); colorText(code,QColor("#eaf3f6"),17);
            code->setAttribute(Qt::WA_TransparentForMouseEvents);
            auto *state=text(stateText(device.value("status").toString()));
            colorText(state,stateColor(device.value("status").toString()),11);
            state->setAttribute(Qt::WA_TransparentForMouseEvents);
            parts->addWidget(code); parts->addWidget(state);
            const auto description=QStringLiteral("%1 / %2 / %3 / 额定 %4 kW")
                .arg(station.value("name").toString(),device.value("code").toString(),state->text(),
                     QString::number(device.value("ratedPowerKw").toDouble(),'g',5));
            tile->setAccessibleName(description); tile->setToolTip(description);
            m_tiles.insert(chargerId,tile);
            connect(tile,&QPushButton::clicked,this,[this,chargerId]{m_selected=chargerId;updateSelection();});
            const int row=1+index/4;
            grid->setRowMinimumHeight(row,60);
            grid->addWidget(tile,row,index%4); ++index;
        }
        if(index==0)grid->addWidget(text(QStringLiteral("该站点暂无设备")),1,0,1,4);
        for(int col=0;col<4;++col)grid->setColumnStretch(col,1);
        m_stationGrid->addWidget(group,groupIndex/2,groupIndex%2);++groupIndex;
    }
    for(int col=0;col<2;++col)m_stationGrid->setColumnStretch(col,1);
    fill(m_table,rows);
    m_empty->setText(loaded?QStringLiteral("当前筛选下没有设备，可切换状态或站点。"):QStringLiteral("等待站点与设备快照…"));
    m_empty->setVisible(rows.isEmpty());
    m_array->verticalScrollBar()->setValue(scroll);
    updateSelection();
}
void FleetStatusPage::updateSelection()
{
    QJsonObject selected;
    for(const auto &value:m_data.value("chargers").toArray())
        if(value.toObject().value("id").toInt()==m_selected) {selected=value.toObject();break;}
    if(selected.isEmpty())m_selected=0;
    for(auto it=m_tiles.begin();it!=m_tiles.end();++it)it.value()->setChecked(it.key()==m_selected);
    {
        const QSignalBlocker blocker(m_table);
        m_table->clearSelection(); m_table->setCurrentCell(-1,-1);
        for(int row=0;row<m_table->rowCount();++row)if(m_table->item(row,0)->text().toInt()==m_selected){m_table->selectRow(row);break;}
    }
    const auto code=selected.value("code").toString();
    m_code->setText(m_selected>0?m_code->fontMetrics().elidedText(code,Qt::ElideRight,210):QStringLiteral("未选择"));
    m_code->setToolTip(code);
    m_stationName->setText(m_selected>0?selected.value("stationName").toString():QStringLiteral("点击阵列或列表中的设备"));
    const QString state=selected.value("status").toString();
    m_state->setText(m_selected>0?stateText(state):QStringLiteral("—")); colorText(m_state,stateColor(state));
    m_rated->setText(m_selected>0?QStringLiteral("%1 kW").arg(selected.value("ratedPowerKw").toDouble(),0,'g',5):QStringLiteral("—"));
    m_count->setText(m_selected>0?QString::number(selected.value("chargeCount").toInteger()):QStringLiteral("—"));
    m_duration->setText(m_selected>0?QStringLiteral("%1 h").arg(selected.value("totalDurationSec").toDouble()/3600,0,'f',1):QStringLiteral("—"));
    m_restart->setText(m_pending?QStringLiteral("正在提交重启…"):QStringLiteral("重启所选故障桩"));
    m_restart->setEnabled(m_fresh&&!m_pending&&m_selected>0&&state=="fault");
}


SystemHealthPage::SystemHealthPage(QWidget *parent):QWidget(parent)
{
    setObjectName("systemHealthPage");
    auto *layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(14);
    auto *heading=new QHBoxLayout;
    auto *title=new QVBoxLayout;
    title->setSpacing(4);
    m_banner=largeText(QStringLiteral("等待运行快照"),"healthHeadline",22); title->addWidget(m_banner);
    m_readAt=text(QStringLiteral("服务、数据库与设备记录分别观测，不使用综合健康分数。"));
    m_readAt->setWordWrap(true); m_readAt->setObjectName("healthReadStatus"); title->addWidget(m_readAt);
    heading->addLayout(title,1);
    auto *refresh=new QPushButton(QStringLiteral("重新检查")); refresh->setObjectName("healthRefreshButton"); heading->addWidget(refresh);
    layout->addLayout(heading);
    auto *viewport=new QScrollArea;
    viewport->setObjectName("healthViewport");
    viewport->setFrameShape(QFrame::NoFrame);
    viewport->setWidgetResizable(true); viewport->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body=new QWidget;
    body->setMinimumHeight(465);
    auto *columns=new QHBoxLayout(body);
    columns->setContentsMargins(0,0,8,0); columns->setSpacing(20);
    auto *observations=new QVBoxLayout; observations->setSpacing(14);
    auto *points=new QHBoxLayout; points->setSpacing(12);
    auto addPoint=[&](const QString &caption,QLabel **value,QLabel **detail,const char *name) {
        auto *card=panel(name);
        auto *content=qobject_cast<QVBoxLayout *>(card->layout());
        content->setContentsMargins(16,15,16,15); content->setSpacing(8);
        content->addWidget(text(caption));
        *value=largeText("—",name,26); content->addWidget(*value);
        *detail=text(QStringLiteral("等待数据"));
        (*detail)->setWordWrap(true); content->addWidget(*detail);
        content->addStretch();
        card->setMinimumHeight(151);
        points->addWidget(card,1);
    };
    addPoint(QStringLiteral("01 / 服务监听"),&m_listener,&m_address,"healthListenValue");
    addPoint(QStringLiteral("02 / 数据库读取"),&m_database,&m_schema,"healthDatabaseValue");
    addPoint(QStringLiteral("03 / 已入库遥测"),&m_sample,&m_sampleTime,"healthTelemetryValue");
    observations->addLayout(points);
    auto *diagnostics=panel("healthDiagnostics");
    auto *details=qobject_cast<QVBoxLayout *>(diagnostics->layout());
    auto *detailHeading=new QHBoxLayout;
    detailHeading->addWidget(text(QStringLiteral("设备状态概览"),"sectionTitle"));
    detailHeading->addStretch();
    auto *toggle=new QPushButton(QStringLiteral("诊断明细")); toggle->setObjectName("healthDetailsToggle"); detailHeading->addWidget(toggle);
    details->addLayout(detailHeading);
    auto *views=new QStackedWidget; views->setObjectName("healthDetailsViews");
    auto *notes=new QWidget;
    auto *noteLayout=new QVBoxLayout(notes); noteLayout->setContentsMargins(0,0,0,0); noteLayout->setSpacing(14);
    m_coverage=text(QStringLiteral("等待设备快照")); m_coverage->setObjectName("healthCoverage"); noteLayout->addWidget(m_coverage);
    auto *stateMetrics=new QHBoxLayout;
    for(const auto &state:stateCodes) {
        auto *column=new QVBoxLayout;
        auto *count=largeText("—",qPrintable("healthCount_"+state),29); colorText(count,stateColor(state),29);
        m_stateCounts.insert(state,count);
        column->addWidget(count); column->addWidget(text(stateText(state)));
        stateMetrics->addLayout(column,1);
    }
    noteLayout->addLayout(stateMetrics);
    m_deviceStrip=new StateStrip(notes); noteLayout->addWidget(m_deviceStrip);
    for(const auto &entry:QList<QPair<QString,QString>>{
        {QStringLiteral("模拟器进程 / 未订阅心跳"),QStringLiteral("已入库遥测不代表进程当前在线；运行与暂停状态请在模拟器面板确认。")},
        {QStringLiteral("数据口径"),QStringLiteral("设备状态来自服务端数据库。预测扩展不参与核心验收；未启用不等于核心故障。")}}) {
        auto *caption=text(entry.first,"sectionTitle");
        caption->setStyleSheet("font-size:14px;color:#c6d8e2;"); noteLayout->addWidget(caption);
        auto *explanation=text(entry.second); explanation->setWordWrap(true); noteLayout->addWidget(explanation);
    }
    noteLayout->addStretch(); views->addWidget(notes);
    auto *diagnosticScroll=new QScrollArea;
    diagnosticScroll->setWidgetResizable(true); diagnosticScroll->setFrameShape(QFrame::NoFrame);
    auto *diagnosticBody=new QWidget;
    auto *tables=new QVBoxLayout(diagnosticBody); tables->setContentsMargins(0,0,0,0); tables->setSpacing(12);
    m_coreTable=table({QStringLiteral("检查项"),QStringLiteral("状态"),QStringLiteral("说明")},"healthTable");
    m_optionalTable=table({QStringLiteral("检查项"),QStringLiteral("状态"),QStringLiteral("说明")},"healthOptionalTable");
    for(auto *item:{m_coreTable,m_optionalTable}) {
        item->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
        item->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Fixed);
        item->setColumnWidth(0,135); item->setColumnWidth(1,90);
        tables->addWidget(item);
    }
    m_coreTable->setMinimumHeight(202); m_optionalTable->setMinimumHeight(126);
    diagnosticScroll->setWidget(diagnosticBody); views->addWidget(diagnosticScroll);
    details->addWidget(views,1);
    observations->addWidget(diagnostics,1);
    columns->addLayout(observations,1);
    auto *queue=panel("healthFaultQueue"); queue->setFixedWidth(268);
    auto *faultLayout=qobject_cast<QVBoxLayout *>(queue->layout());
    faultLayout->addWidget(text(QStringLiteral("待处置故障"),"sectionTitle"));
    m_faultCount=largeText("—","healthFaultCount",49); colorText(m_faultCount,QColor("#f0af8e"),49);
    faultLayout->addWidget(m_faultCount);
    m_faultHint=text(QStringLiteral("等待设备状态")); m_faultHint->setWordWrap(true); faultLayout->addWidget(m_faultHint);
    m_faults=new QListWidget;
    m_faults->setObjectName("healthFaultList");
    m_faults->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_faults->setTextElideMode(Qt::ElideRight);
    m_faults->setStyleSheet("QListWidget{background:transparent;border:0;} QListWidget::item{padding:10px 6px;border-bottom:1px solid #304b5b;} QListWidget::item:selected{background:#234858;}");
    faultLayout->addWidget(m_faults,1);
    m_locate=new QPushButton(QStringLiteral("定位到设备阵列"));
    m_locate->setObjectName("healthLocateFault"); m_locate->setProperty("role","primary");
    m_locate->setStyleSheet("QPushButton{color:#102d35;} QPushButton:disabled{color:#97adbc;}");
    m_locate->setEnabled(false); faultLayout->addWidget(m_locate);
    auto *logs=new QPushButton(QStringLiteral("查看请求日志")); logs->setObjectName("healthLogsButton"); faultLayout->addWidget(logs);
    columns->addWidget(queue);
    viewport->setWidget(body); layout->addWidget(viewport,1);
    auto *maintenance=panel("adminDangerZone");
    maintenance->setStyleSheet("QFrame#adminDangerZone{background:#263039;border:1px solid #73514e;border-radius:10px;}");
    auto *maintenanceLayout=qobject_cast<QVBoxLayout *>(maintenance->layout());
    maintenanceLayout->setSpacing(8); maintenanceLayout->setContentsMargins(18,12,18,12);
    auto *maintenanceTitle=new QHBoxLayout;
    auto *dangerTitle=text(QStringLiteral("演示环境维护"),"sectionTitle"); colorText(dangerTitle,QColor("#f0af8e"),16);
    maintenanceTitle->addWidget(dangerTitle); maintenanceTitle->addStretch();
    m_reset=new QPushButton(QStringLiteral("恢复演示黄金数据"));
    m_reset->setObjectName("demoResetButton"); m_reset->setProperty("role","danger");
    maintenanceTitle->addWidget(m_reset);
    maintenanceLayout->addLayout(maintenanceTitle);
    auto *warning=text(QStringLiteral("将清除当前用户变更、订单、设备事件和旧请求日志。此操作需二次确认，仅在重新准备演示时使用。"));
    warning->setWordWrap(true); maintenanceLayout->addWidget(warning);
    layout->addWidget(maintenance);
    connect(refresh,&QPushButton::clicked,this,&SystemHealthPage::refreshRequested);
    connect(logs,&QPushButton::clicked,this,&SystemHealthPage::logsRequested);
    connect(toggle,&QPushButton::clicked,this,[views,toggle] {
        views->setCurrentIndex(1-views->currentIndex());
        toggle->setText(views->currentIndex()==0?QStringLiteral("诊断明细"):QStringLiteral("观测说明"));
    });
    connect(m_faults,&QListWidget::itemSelectionChanged,this,&SystemHealthPage::updateFaultSelection);
    connect(m_locate,&QPushButton::clicked,this,[this]{
        if(auto *item=m_faults->currentItem();item&&m_locate->isEnabled())emit locateChargerRequested(item->data(Qt::UserRole).toInt());
    });
}
void SystemHealthPage::setData(const QJsonObject &data,const QJsonObject &health,bool listening,const QString &address)
{
    QJsonObject counts;
    for(const auto &entry:data.value("chargers").toArray()) {
        const auto state=entry.toObject().value("status").toString();
        const QString category=stateCodes.contains(state)?state:QString("unknown");
        counts[category]=counts.value(category).toInt()+1;
        counts["total"]=counts.value("total").toInt()+1;
    }
    m_coverage->setText(QStringLiteral("%1 个站点 / %2 台设备 · 数据库当前状态分布")
        .arg(data.value("stations").toArray().size()).arg(data.value("chargers").toArray().size()));
    for(auto it=m_stateCounts.begin();it!=m_stateCounts.end();++it)it.value()->setText(QString::number(counts.value(it.key()).toInt()));
    m_deviceStrip->setCounts(counts);
    m_banner->setText(listening?QStringLiteral("本机服务可读，设备状态已同步"):QStringLiteral("服务监听已停止，请检查运行环境"));
    colorText(m_banner,listening?QColor("#eaf3f6"):QColor("#f0af8e"),22);
    m_readAt->setText(QStringLiteral("检查于 %1  /  本地数据库读取成功；不代表全部客户端已接入").arg(clockText(data.value("readAt").toString())));
    colorText(m_readAt,QColor("#97adbc"),12);
    m_listener->setText(listening?QStringLiteral("监听中"):QStringLiteral("已停止"));
    colorText(m_listener,listening?QColor("#72e0c7"):QColor("#f0af8e"),26);
    m_address->setText(address);
    m_database->setText(QStringLiteral("可读取")); colorText(m_database,QColor("#72e0c7"),26);
    m_schema->setText(QStringLiteral("Schema %1\n快照版本 %2").arg(health.value("schemaVersion").toInt()).arg(health.value("snapshotVersion").toInt()));
    const auto telemetry=data.value("latestTelemetry").toObject();
    m_sample->setText(telemetry.isEmpty()?QStringLiteral("尚无记录"):QStringLiteral("已有记录"));
    colorText(m_sample,QColor("#c6d8e2"),24);
    m_sampleTime->setText(telemetry.isEmpty()?QStringLiteral("未发现遥测样本\n不据此判定模拟器离线"):
        QStringLiteral("设备 #%1 / %2\n设备时间，不是在线心跳").arg(telemetry.value("chargerId").toInt()).arg(clockText(telemetry.value("recordedAt").toString())));
    m_sampleTime->setToolTip(telemetry.value("recordedAt").toString());
    fill(m_coreTable,{{QStringLiteral("运行上下文"),"active",QStringLiteral("只读快照成功；完整业务仍需联演验证")},
        {QStringLiteral("服务监听"),listening?"active":"unverified",address},
        {QStringLiteral("数据库 schema"),"active",QString::number(health.value("schemaVersion").toInt())},
        {QStringLiteral("快照版本"),"active",QString::number(health.value("snapshotVersion").toInt())}});
    const bool forecast=!health.value("forecastRunId").toString().isEmpty();
    fill(m_optionalTable,{{QStringLiteral("扩展预测"),forecast?health.value("status").toString():QString("disabled"),
            QStringLiteral("不参与核心验收 · ")+(forecast?health.value("forecastRunId").toString():QStringLiteral("无活动预测批次"))},
        {QStringLiteral("模拟器状态"),"unverified",QStringLiteral("未订阅进程心跳；已入库遥测不代表进程在线")}});
    const int selected=m_faults->currentItem()?m_faults->currentItem()->data(Qt::UserRole).toInt():0;
    int faults=0,restarting=0;
    {
        const QSignalBlocker blocker(m_faults);
        m_faults->clear();
        for(const auto &value:data.value("chargers").toArray()) {
            const auto device=value.toObject(); const auto status=device.value("status").toString();
            if(status!="fault"&&status!="restarting")continue;
            faults+=status=="fault";restarting+=status=="restarting";
            auto *item=new QListWidgetItem(QStringLiteral("%1   %2\n%3")
                .arg(device.value("code").toString(),stateText(status),device.value("stationName").toString()));
            item->setData(Qt::UserRole,device.value("id").toInt());
            item->setToolTip(item->text()); item->setForeground(stateColor(status));
            m_faults->addItem(item);
            if(item->data(Qt::UserRole).toInt()==selected)m_faults->setCurrentItem(item);
        }
    }
    m_faultCount->setText(QString::number(faults));
    m_faultHint->setText(faults+restarting==0?QStringLiteral("当前没有故障或重启中的设备。"):
        QStringLiteral("%1 台故障 / %2 台重启中\n选中设备后可定位并处理。").arg(faults).arg(restarting));
    m_faults->setEnabled(true);
    updateFaultSelection();
}
void SystemHealthPage::setReadError(const QString &message)
{
    m_banner->setText(QStringLiteral("运行快照读取失败"));
    colorText(m_banner,QColor("#f0af8e"),22);
    m_readAt->setText(QStringLiteral("旧信息仅供参考 · ")+message); colorText(m_readAt,QColor("#f0af8e"),12);
    m_database->setText(QStringLiteral("读取失败")); colorText(m_database,QColor("#f0af8e"),24);
    m_listener->setText(QStringLiteral("待核对")); colorText(m_listener,QColor("#97adbc"),24);
    m_sample->setText(QStringLiteral("待核对")); colorText(m_sample,QColor("#97adbc"),24);
    m_faults->setEnabled(false); m_locate->setEnabled(false);
}
void SystemHealthPage::updateFaultSelection()
{
    m_locate->setEnabled(m_faults->isEnabled()&&m_faults->currentItem()&&m_faults->currentItem()->isSelected());
}
