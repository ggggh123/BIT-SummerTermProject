#include "app/UserAppConfig.h"
#include "domain/Formatters.h"
#include "ui/MainWindow.h"

#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class EnvironmentVariableGuard final {
public:
    explicit EnvironmentVariableGuard(const char *name)
        : m_name(name), m_wasSet(qEnvironmentVariableIsSet(name)), m_previous(qgetenv(name)) {}

    ~EnvironmentVariableGuard() {
        if (m_wasSet) {
            qputenv(m_name, m_previous);
        } else {
            qunsetenv(m_name);
        }
    }

private:
    const char *m_name;
    bool m_wasSet;
    QByteArray m_previous;
};

class FormattersTest final : public QObject {
    Q_OBJECT

private slots:
    void phoneAndMoney();
    void moneyUsesCheckedSafeIntegerArithmetic();
    void moneyRejectsNonCanonicalOrNonAsciiText();
    void distanceIsStable();
    void environmentOverridesLocalIni();
    void missingConfigurationIsVisibleInChinese();
};

void FormattersTest::phoneAndMoney() {
    QVERIFY(isValidPhone(QStringLiteral("13800138000")));
    QVERIFY(!isValidPhone(QStringLiteral("1380013800")));
    QCOMPARE(parsePositiveFen(QStringLiteral("12.34")).value(), qint64{1234});
    QVERIFY(!parsePositiveFen(QStringLiteral("12.345")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("0")).has_value());
    QCOMPARE(formatFen(1234), QStringLiteral("12.34"));
}

void FormattersTest::moneyUsesCheckedSafeIntegerArithmetic() {
    const auto minimumFen = parsePositiveFen(QStringLiteral("0.01"));
    QVERIFY(minimumFen.has_value());
    QCOMPARE(*minimumFen, qint64{1});
    QCOMPARE(parsePositiveFen(QStringLiteral("1.2")).value(), qint64{120});
    const auto maximumFen = parsePositiveFen(QStringLiteral("90071992547409.91"));
    QVERIFY(maximumFen.has_value());
    QCOMPARE(*maximumFen, qint64{9'007'199'254'740'991LL});
    QVERIFY(!parsePositiveFen(QStringLiteral("90071992547409.92")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("90071992547410")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("92233720368547758.08")).has_value());
}

void FormattersTest::moneyRejectsNonCanonicalOrNonAsciiText() {
    QVERIFY(!parsePositiveFen(QStringLiteral("12.")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("0.00")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("00.01")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("+1")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("-1")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral(" 1")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("1 ")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("1e2")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("１.００")).has_value());
    QVERIFY(!parsePositiveFen(QStringLiteral("0.001")).has_value());
}

void FormattersTest::distanceIsStable() {
    QCOMPARE(qRound(haversineKm(39.9042, 116.4074, 39.9142, 116.4074) * 10.0), 11);
}

void FormattersTest::environmentOverridesLocalIni() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString iniPath = temporaryDirectory.filePath(QStringLiteral("config.local.ini"));
    QSettings settings(iniPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("server/host"), QStringLiteral("ini.example"));
    settings.setValue(QStringLiteral("server/port"), 9100);
    settings.setValue(QStringLiteral("tencent/mapKey"), QStringLiteral("ini-key"));
    settings.sync();

    EnvironmentVariableGuard hostGuard("EV_SERVER_HOST");
    EnvironmentVariableGuard portGuard("EV_SERVER_PORT");
    EnvironmentVariableGuard keyGuard("EV_TENCENT_MAP_KEY");
    qputenv("EV_SERVER_HOST", "env.example");
    qputenv("EV_SERVER_PORT", "9200");
    qputenv("EV_TENCENT_MAP_KEY", "env-key");

    const UserAppConfig config = UserAppConfig::load(iniPath);
    QCOMPARE(config.serverHost, QStringLiteral("env.example"));
    QCOMPARE(config.serverPort, quint16{9200});
    QCOMPARE(config.tencentMapKey, QStringLiteral("env-key"));
    QVERIFY(config.isValid());
}

void FormattersTest::missingConfigurationIsVisibleInChinese() {
    EnvironmentVariableGuard hostGuard("EV_SERVER_HOST");
    EnvironmentVariableGuard portGuard("EV_SERVER_PORT");
    EnvironmentVariableGuard keyGuard("EV_TENCENT_MAP_KEY");
    qunsetenv("EV_SERVER_HOST");
    qunsetenv("EV_SERVER_PORT");
    qunsetenv("EV_TENCENT_MAP_KEY");

    const UserAppConfig config = UserAppConfig::load(QStringLiteral("/nonexistent/config.local.ini"));
    QVERIFY(!config.isValid());

    MainWindow window(config);
    const auto *message = window.findChild<QLabel *>(QStringLiteral("configurationMessage"));
    QVERIFY(message != nullptr);
    QVERIFY(message->text().contains(QStringLiteral("缺少服务器地址")));
    QVERIFY(message->text().contains(QStringLiteral("缺少腾讯地图密钥")));
}

QTEST_MAIN(FormattersTest)

#include "tst_formatters.moc"
