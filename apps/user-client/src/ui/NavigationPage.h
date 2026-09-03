#pragma once

#include "domain/Models.h"

#include <QDateTime>
#include <QUrl>
#include <QWidget>

#include <optional>
#include <memory>

struct LastRoute final {
    ev::user::GeoPoint origin;
    ev::user::GeoPoint destination;
    QString stationName;
    QString mode;
    QDateTime generatedAt;
};

class RouteOperationTracker final
{
public:
    void begin(QString operationId, LastRoute candidate);
    [[nodiscard]] bool complete(const QString &operationId, const QString &state,
                                const QDateTime &generatedAt);
    [[nodiscard]] std::optional<LastRoute> lastSuccessfulRoute() const;
    [[nodiscard]] std::optional<LastRoute> retryRoute() const;
    void invalidatePending();
    void resetForSession();

private:
    QString currentOperationId_;
    std::optional<LastRoute> pendingRoute_;
    std::optional<LastRoute> retryRoute_;
    std::optional<LastRoute> lastSuccessfulRoute_;
};

class NavigationPage final : public QWidget
{
    Q_OBJECT

public:
    explicit NavigationPage(QString mapKey, QWidget *parent = nullptr);
    ~NavigationPage() override;

    [[nodiscard]] static QUrl pageUrl();
    [[nodiscard]] static QString buildConfigureMapScript(const QString &key,
                                                         const QString &operationId = {});
    [[nodiscard]] static QString buildRenderRouteScript(
        const ev::user::GeoPoint &from, const ev::user::GeoPoint &to, const QString &mode,
        const QString &stationName, const QString &operationId = {}, QString *error = nullptr);
    [[nodiscard]] static QString buildOperationStatusScript(const QString &operationId);
    [[nodiscard]] static QString buildInvalidateRouteScript(const QString &operationId);
    [[nodiscard]] static QString buildResetRouteSessionScript();
    [[nodiscard]] std::optional<LastRoute> lastSuccessfulRoute() const;

public slots:
    void showRoute(ev::user::GeoPoint origin, ev::user::Station station,
                   QString mode = QStringLiteral("driving"));
    void deactivate();
    void resetForSession();

signals:
    void backRequested();

private:
    friend class TencentMapClientTest;

    enum class OperationKind { Configure, Route };

    NavigationPage(QString mapKey, QString documentReadyBootstrapScript, QWidget *parent);
    void configureForCurrentLoad();
    void executePendingRoute();
    void pollOperation(const QString &operationId, OperationKind kind, int attemptsRemaining = 300);
    void finishOperation(const QString &operationId, OperationKind kind, const QString &state);
    void showFailure(const QString &reason);
    void updateLastSuccessLabel();
    void invalidateRouteAttempt();

    struct CallbackGate final {
        bool active = true;
    };

    QString mapKey_;
    QString documentReadyBootstrapScript_;
    class QWebEngineView *view_;
    class QLabel *statusLabel_;
    class QLabel *cacheLabel_;
    class QPushButton *retryButton_;
    class QComboBox *modeBox_;
    bool pageLoaded_ = false;
    bool configurationStarted_ = false;
    bool configured_ = false;
    QString configureOperationId_;
    QString routeOperationId_;
    RouteOperationTracker routeTracker_;
    quint64 nextOperationId_ = 0;
    std::shared_ptr<CallbackGate> callbackGate_;
};

Q_DECLARE_METATYPE(LastRoute)
