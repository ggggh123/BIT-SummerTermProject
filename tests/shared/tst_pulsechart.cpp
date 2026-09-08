#include "ui/PulseChart.h"
#include <QSignalSpy>
#include <QtTest>

class PulseChartTest : public QObject {
    Q_OBJECT
private slots:
    void readingsFollowSamplesAndNeverBridgeMissingIntervals()
    {
        ev::ui::PulseChart chart;
        chart.resize(800,300);
        chart.show();
        ev::ui::PowerSeries series;
        series.points={{0,0,0},{60,30,0.25},{120,60,1},{400,10,2}};
        chart.setSeries({series},400);
        QCOMPARE(chart.reading(0)->kw,10.0);
        QSignalSpy moved(&chart,&ev::ui::PulseChart::cursorChanged);
        QTest::keyClick(&chart,Qt::Key_Home);
        QCOMPARE(chart.reading(0)->kw,0.0);
        for(int i=0;i<15;++i) QTest::keyClick(&chart,Qt::Key_Right);
        QCOMPARE(chart.cursorSeconds(),60.0);
        QCOMPARE(chart.reading(0)->energy,0.25);
        for(int i=0;i<40;++i) QTest::keyClick(&chart,Qt::Key_Right);
        QVERIFY(!chart.reading(0).has_value());
        chart.followLatest();
        QVERIFY(chart.followingLatest());
        QVERIFY(moved.count()>0);
        chart.setSeries({series},430);
        QVERIFY(!chart.reading(0).has_value()); // 超过末点 15 秒不可伪造读数。
        chart.setSeries({},430);
        QCOMPARE(chart.sampleCount(),0);
        QVERIFY(!chart.reading(0).has_value());
    }
};
QTEST_MAIN(PulseChartTest)
#include "tst_pulsechart.moc"
