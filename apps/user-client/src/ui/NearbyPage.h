#pragma once

#include "domain/Models.h"

#include <QHash>
#include <QWidget>

#include <optional>

class TencentMapClient;
class UserApi;

class NearbyPage final : public QWidget
{
    Q_OBJECT

public:
    explicit NearbyPage(UserApi *userApi, TencentMapClient *mapClient, QWidget *parent = nullptr);

    [[nodiscard]] static QStringList presetAddresses();
    [[nodiscard]] static QString forecastText(const ev::user::Station &station,
                                              const ev::user::ForecastLatestResult &forecast);

public slots:
    void setConnectionAvailable(bool available);
    void cancelChargeRefresh();
    void refreshAfterCharge(ev::user::GeoPoint origin, qint64 stationId,
                            quint64 selectionGeneration, quint64 refreshAttemptId = 0);
    void applyChargeStationDetail(ev::user::StationDetailResult result,
                                  quint64 selectionGeneration, quint64 refreshAttemptId);
    void displayStations(ev::user::StationListResult result);
    void displayForecast(ev::user::ForecastLatestResult result);
    void displayStationDetail(ev::user::StationDetailResult result);

signals:
    void chargerSelected(ev::user::StationSelection selection);
    void navigationRequested(ev::user::GeoPoint origin, ev::user::Station station);
    void selectionInvalidated(quint64 selectionGeneration);
    void chargeRefreshCommitted(quint64 refreshAttemptId, quint64 selectionGeneration,
                                qint64 stationId);
    void chargeRefreshFailed(quint64 refreshAttemptId, quint64 selectionGeneration,
                             qint64 stationId, ev::user::ApiError error);
    void chargeRefreshUnavailable(quint64 refreshAttemptId, quint64 selectionGeneration,
                                  qint64 stationId);

private:
    friend class UserApiTest;

    void searchCurrentAddress();
    void requestNearbyStations(
        const ev::user::GeoPoint &origin,
        std::optional<quint64> requiredSelectionGeneration = std::nullopt,
        std::optional<qint64> expectedStationId = std::nullopt,
        quint64 refreshAttemptId = 0);
    void requestStationDetail(const ev::user::Station &station);
    void cancelPendingStationList();
    void abandonChargeRefresh();
    void clearDisplayedDetailForMissingStation();
    void applyStations(ev::user::StationListResult result, bool invalidateCurrentSelection);
    void applyStationDetail(ev::user::StationDetailResult result, bool invalidateCurrentSelection);
    void finishChargeRefreshIfReady();
    void finishChargeRefreshFailed(const ev::user::ApiError &error);
    void finishChargeRefreshUnavailable();
    void resetChargeRefresh();
    void invalidateSelection();
    void rebuildStationCards();
    void showError(const QString &message);
    void setSearchPending(bool pending);
    void setDetailPending(bool pending);
    void setDetailControlsEnabled(bool enabled);
    void handleApiFailure(const ev::user::ApiError &error);
    [[nodiscard]] static QString errorText(const ev::user::ApiError &error);

    UserApi *userApi_;
    TencentMapClient *mapClient_;
    class QComboBox *addressBox_;
    class QPushButton *searchButton_;
    class QLabel *connectionBanner_;
    class QLabel *statusLabel_;
    class QLabel *detailStatus_;
    class QVBoxLayout *stationLayout_;
    class QVBoxLayout *detailLayout_;
    QString pendingGeocodeId_;
    QString pendingStationsRequestId_;
    QString pendingForecastRequestId_;
    QString pendingDetailRequestId_;
    quint64 originGeneration_ = 0;
    quint64 searchGeneration_ = 0;
    quint64 selectionGeneration_ = 0;
    quint64 pendingStationsOriginGeneration_ = 0;
    quint64 pendingStationsSearchGeneration_ = 0;
    std::optional<quint64> pendingStationsSelectionGeneration_;
    std::optional<quint64> pendingForecastSelectionGeneration_;
    std::optional<quint64> pendingForecastRefreshAttemptId_;
    std::optional<quint64> chargeRefreshAttemptId_;
    std::optional<quint64> chargeRefreshSelectionGeneration_;
    std::optional<qint64> chargeRefreshStationId_;
    quint64 chargeRefreshOriginGeneration_ = 0;
    quint64 chargeRefreshSearchGeneration_ = 0;
    bool chargeListApplied_ = false;
    bool chargeDetailApplied_ = false;
    std::optional<ev::user::StationDetailResult> chargeBufferedDetail_;
    quint64 pendingForecastOriginGeneration_ = 0;
    quint64 pendingForecastSearchGeneration_ = 0;
    quint64 pendingDetailOriginGeneration_ = 0;
    quint64 pendingDetailSearchGeneration_ = 0;
    quint64 pendingDetailSelectionGeneration_ = 0;
    std::optional<ev::user::GeoPoint> origin_;
    std::optional<ev::user::GeoPoint> pendingDetailOrigin_;
    std::optional<ev::user::GeoPoint> displayedDetailOrigin_;
    QVector<ev::user::Station> stations_;
    ev::user::ForecastLatestResult forecast_;
};
