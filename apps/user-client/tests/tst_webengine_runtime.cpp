#include "app/WebEngineRuntime.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr auto kChromiumFlagsEnvironment = "QTWEBENGINE_CHROMIUM_FLAGS";

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
} // namespace

class WebEngineRuntimeTest final : public QObject {
    Q_OBJECT

private slots:
    void vmwareAppendsRequiredTokenAndPreservesExistingFlags();
    void exactTokenIsNotDuplicated();
    void similarPrefixDoesNotCountAsTheRequiredToken();
    void nonVmwareVendorIsUnchanged();
    void similarVendorNameIsUnchanged();
    void applyReadsVendorAndUpdatesTheEnvironment();
    void unreadableVendorHasNoSideEffect();
};

void WebEngineRuntimeTest::vmwareAppendsRequiredTokenAndPreservesExistingFlags() {
    QCOMPARE(
        WebEngineRuntime::chromiumFlagsForSystem(
            QByteArrayLiteral("--existing-test-flag=value"), QByteArrayLiteral("VMware, Inc.\n")),
        QByteArrayLiteral("--existing-test-flag=value --ignore-gpu-blocklist"));
}

void WebEngineRuntimeTest::exactTokenIsNotDuplicated() {
    QCOMPARE(
        WebEngineRuntime::chromiumFlagsForSystem(
            QByteArrayLiteral("--first --ignore-gpu-blocklist --last"),
            QByteArrayLiteral("VMware, Inc.")),
        QByteArrayLiteral("--first --ignore-gpu-blocklist --last"));
}

void WebEngineRuntimeTest::similarPrefixDoesNotCountAsTheRequiredToken() {
    QCOMPARE(
        WebEngineRuntime::chromiumFlagsForSystem(
            QByteArrayLiteral("--ignore-gpu-blocklist=temporary"),
            QByteArrayLiteral("VMware, Inc.")),
        QByteArrayLiteral("--ignore-gpu-blocklist=temporary --ignore-gpu-blocklist"));
}

void WebEngineRuntimeTest::nonVmwareVendorIsUnchanged() {
    const QByteArray existingFlags("--existing-test-flag=value  ");
    QCOMPARE(
        WebEngineRuntime::chromiumFlagsForSystem(existingFlags, QByteArrayLiteral("QEMU")),
        existingFlags);
}

void WebEngineRuntimeTest::similarVendorNameIsUnchanged() {
    const QByteArray existingFlags("--existing-test-flag=value");
    QCOMPARE(
        WebEngineRuntime::chromiumFlagsForSystem(
            existingFlags, QByteArrayLiteral("VMwareCompatible Systems")),
        existingFlags);
}

void WebEngineRuntimeTest::applyReadsVendorAndUpdatesTheEnvironment() {
    EnvironmentVariableGuard environmentGuard(kChromiumFlagsEnvironment);
    qputenv(kChromiumFlagsEnvironment, "--existing-test-flag=value");

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString vendorPath = temporaryDirectory.filePath(QStringLiteral("sys_vendor"));
    QFile vendorFile(vendorPath);
    QVERIFY(vendorFile.open(QIODevice::WriteOnly));
    QCOMPARE(vendorFile.write("VMware, Inc.\n"), qint64{13});
    vendorFile.close();

    WebEngineRuntime::applySystemCompatibility(vendorPath);

    QCOMPARE(
        qgetenv(kChromiumFlagsEnvironment),
        QByteArrayLiteral("--existing-test-flag=value --ignore-gpu-blocklist"));
}

void WebEngineRuntimeTest::unreadableVendorHasNoSideEffect() {
    EnvironmentVariableGuard environmentGuard(kChromiumFlagsEnvironment);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString missingPath = temporaryDirectory.filePath(QStringLiteral("missing-sys-vendor"));
    QVERIFY(!QFile::exists(missingPath));

    qputenv(kChromiumFlagsEnvironment, "--existing-test-flag=value");
    WebEngineRuntime::applySystemCompatibility(missingPath);
    QCOMPARE(qgetenv(kChromiumFlagsEnvironment), QByteArrayLiteral("--existing-test-flag=value"));

    qunsetenv(kChromiumFlagsEnvironment);
    WebEngineRuntime::applySystemCompatibility(missingPath);
    QVERIFY(!qEnvironmentVariableIsSet(kChromiumFlagsEnvironment));
}

QTEST_APPLESS_MAIN(WebEngineRuntimeTest)

#include "tst_webengine_runtime.moc"
