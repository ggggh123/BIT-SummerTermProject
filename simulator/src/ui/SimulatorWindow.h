#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "core/TelemetryEngine.h"
#include "net/SimulatorClient.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTimer;
namespace ev::ui { class PulseChart; }

namespace ev::simulator {

class SimulatorWindow : public QWidget
{
    Q_OBJECT
public:
    SimulatorWindow(ISimulatorClient *client, TelemetryEngine *engine,
                    QWidget *parent = nullptr);

    QString runButtonText() const;
    int tickCount() const;
    bool faultEnabled() const;
    bool recoverEnabled() const;
    QStringList logLines() const;

public slots:
    void toggleRun();
    void doTick();
    void injectFault();
    void injectRecovery();
    void refreshStatus();
    void prepareReset();

private slots:
    void onLog(const QString &message);
    void onChargersReceived(const QList<ChargerSnapshot> &chargers);
    void onSelectionChanged();

private:
    void updateChargerTable();
    void updateRunState();
    void updateChart();
    void updateReading();
    void setSessionState(const QString &text, const QString &tone);
    void drainIntents();
    int selectedChargerId() const;

    ISimulatorClient *client_;
    TelemetryEngine *engine_;
    bool running_ = false;
    int tickCount_ = 0;
    qint64 sampleCount_ = 0;
    qint64 chartStartMs_ = 0;
    struct Batch {
        QDateTime recordedAt;
        QMap<int, TelemetrySample> samples;
    };
    QVector<Batch> batches_;
    QMap<int, TelemetrySample> latestSamples_;

    QLabel *badge_;
    QLabel *timeLabel_;
    QLabel *eventLabel_;
    QLabel *sampleLabel_;
    QLabel *runState_;
    QLabel *selectedLabel_;
    QLabel *selectedState_;
    QLabel *selectedRated_;
    QLabel *selectedPower_;
    QLabel *powerLabel_;
    QLabel *scopeLabel_;
    QLabel *cursorLabel_;
    QLabel *fleetLabel_;
    QLabel *emptyLabel_;
    QLabel *logEmptyLabel_;
    QPushButton *runButton_;
    QPushButton *faultButton_;
    QPushButton *recoverButton_;
    QPushButton *refreshButton_;
    QPushButton *resetButton_;
    QTableWidget *table_;
    QListWidget *logList_;
    QTimer *tickTimer_;
    ev::ui::PulseChart *chart_;
    QStringList logLines_;
};

} // namespace ev::simulator
