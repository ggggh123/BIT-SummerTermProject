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
    void displayStations(ev::user::StationListResult result);
    void displayForecast(ev::user::ForecastLatestResult result);
    void displayStationDetail(ev::user::StationDetailResult result);

signals:
    void chargerSelected(ev::user::StationSelection selection);
    void navigationRequested(ev::user::GeoPoint origin, ev::user::Station station);

private:
    void searchCurrentAddress();
    void rebuildStationCards();
    void showError(const QString &message);

    UserApi *userApi_;
    TencentMapClient *mapClient_;
    class QComboBox *addressBox_;
    class QPushButton *searchButton_;
    class QLabel *statusLabel_;
    class QVBoxLayout *stationLayout_;
    class QVBoxLayout *detailLayout_;
    QString pendingGeocodeId_;
    std::optional<ev::user::GeoPoint> origin_;
    std::optional<qint64> requestedStationId_;
    QVector<ev::user::Station> stations_;
    ev::user::ForecastLatestResult forecast_;
};
