#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
class QLabel;
class QPushButton;
class QComboBox;
class QGridLayout;
namespace ev::ui { class PulseChart; }

class EnergyPage final : public QWidget {
    Q_OBJECT
public:
    explicit EnergyPage(QWidget *parent=nullptr);
    qint64 stationId() const;
    void setData(const QJsonObject &data);
signals:
    void stationChanged();
private:
    void selectDevice(qint64 id);
    void renderChart();
    void renderReading();
    QJsonArray m_devices;
    QComboBox *m_station;
    QLabel *m_sampleNote,*m_total,*m_deviceName,*m_deviceStatus,*m_power,*m_energy,*m_readingNote;
    QLabel *m_legend;
    QPushButton *m_detail,*m_latest;
    QGridLayout *m_deviceGrid;
    QVector<QPushButton *> m_deviceButtons;
    ev::ui::PulseChart *m_chart;
    qint64 m_selected=0;
    QVector<qint64> m_seriesIds;
    double m_startMs=0;
    QString m_latestAt;
};
