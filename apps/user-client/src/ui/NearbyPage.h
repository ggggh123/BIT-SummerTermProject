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
    void displayStations(ev::user::StationListResult result);
    void displayForecast(ev::user::ForecastLatestResult result);
    void displayStationDetail(ev::user::StationDetailResult result);

signals:
    void chargerSelected(ev::user::StationSelection selection);
    void navigationRequested(ev::user::GeoPoint origin, ev::user::Station station);

private:
    friend class UserApiTest;

    void searchCurrentAddress();
    void requestNearbyStations(const ev::user::GeoPoint &origin);
    void requestStationDetail(const ev::user::Station &station);
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
