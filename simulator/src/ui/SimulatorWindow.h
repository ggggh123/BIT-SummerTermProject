#pragma once

#include <QStringList>
#include <QWidget>

#include "core/TelemetryEngine.h"
#include "net/SimulatorClient.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTimer;

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
    void drainIntents();
    int selectedChargerId() const;

    ISimulatorClient *client_;
    TelemetryEngine *engine_;
    bool running_ = false;
    int tickCount_ = 0;

    QLabel *badge_;
    QLabel *timeLabel_;
    QLabel *eventLabel_;
    QPushButton *runButton_;
    QPushButton *faultButton_;
    QPushButton *recoverButton_;
    QPushButton *refreshButton_;
    QPushButton *resetButton_;
    QTableWidget *table_;
    QListWidget *logList_;
    QTimer *tickTimer_;
    QStringList logLines_;
};

} // namespace ev::simulator
