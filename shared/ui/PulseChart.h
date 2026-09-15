#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>
#include <optional>

namespace ev::ui {
struct PowerPoint { double seconds=0, kw=0, energy=0; };
struct PowerSeries {
    QString id, name;
    QColor color=QColor("#72e0c7");
    QVector<PowerPoint> points;
};

// 原生 QWidget 数据图，不依赖 Qt Charts / WebEngine / GPU。
class PulseChart final : public QWidget {
    Q_OBJECT
public:
    explicit PulseChart(bool compact=false,QWidget *parent=nullptr);
    void setSeries(QVector<PowerSeries> series,double durationSeconds,double minimumMaxKw=60);
    void setMessage(const QString &message);
    void setPreserveAspectRatio(bool preserve) { m_preserveAspectRatio=preserve;update(); }
    void followLatest();
    void setCursorSeconds(double seconds);
    double cursorSeconds() const;
    bool followingLatest() const { return m_cursor<0; }
    std::optional<PowerPoint> reading(int seriesIndex) const;
    int sampleCount() const;
    QSize minimumSizeHint() const override;
    static QString elapsed(double seconds);
signals:
    void cursorChanged();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    std::optional<PowerPoint> at(int seriesIndex,double seconds) const;
    QRectF plotRect() const;
    void choose(double x);
    QVector<PowerSeries> m_series;
    QString m_message;
    double m_duration=1600,m_max=60,m_cursor=-1;
    bool m_compact=false;
    bool m_preserveAspectRatio=true;
};
} // namespace ev::ui
