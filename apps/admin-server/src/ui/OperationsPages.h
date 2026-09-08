#pragma once
#include <QJsonObject>
#include <QHash>
#include <QWidget>

class QComboBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTableWidget;
class StateStrip;

class FleetStatusPage final : public QWidget {
    Q_OBJECT
public:
    explicit FleetStatusPage(QWidget *parent=nullptr);
    void setData(const QJsonObject &data);
    void setReadError(const QString &message);
    void setRestartPending(bool pending);
    void selectCharger(int id);
    int selectedChargerId() const { return m_selected; }
signals:
    void refreshRequested();
    void restartRequested(int id);
    void logsRequested();
private:
    void render();
    void updateSelection();
    QJsonObject m_data;
    bool m_fresh=false, m_pending=false;
    int m_selected=0;
    QString m_status;
    QComboBox *m_station;
    QHash<QString,QPushButton *> m_filters;
    QHash<int,QPushButton *> m_tiles;
    QGridLayout *m_stationGrid;
    QScrollArea *m_array;
    QStackedWidget *m_views;
    QTableWidget *m_table;
    QLabel *m_total, *m_meta, *m_empty, *m_code, *m_stationName, *m_state, *m_rated, *m_count, *m_duration;
    QPushButton *m_restart;
    StateStrip *m_strip;
};

class SystemHealthPage final : public QWidget {
    Q_OBJECT
public:
    explicit SystemHealthPage(QWidget *parent=nullptr);
    void setData(const QJsonObject &data, const QJsonObject &health, bool listening, const QString &address);
    void setReadError(const QString &message);
    QPushButton *resetButton() const { return m_reset; }
signals:
    void refreshRequested();
    void locateChargerRequested(int id);
    void logsRequested();
private:
    void updateFaultSelection();
    QLabel *m_readAt, *m_banner, *m_listener, *m_address, *m_database, *m_schema, *m_sample, *m_sampleTime;
    QLabel *m_faultCount, *m_faultHint;
    QListWidget *m_faults;
    QPushButton *m_locate, *m_reset;
    QTableWidget *m_coreTable, *m_optionalTable;
    QHash<QString,QLabel *> m_stateCounts;
    StateStrip *m_deviceStrip;
    QLabel *m_coverage;
};
