#include "app/AppContext.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

namespace {

bool isHeadlessRequested(int argc, char *argv[])
{
    const QStringList headlessOptions = {
        QStringLiteral("--server"),
        QStringLiteral("--no-gui")
    };

    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (headlessOptions.contains(argument)) {
            return true;
        }
    }
    return false;
}

void configureParser(QCommandLineParser *parser)
{
    parser->setApplicationDescription(QStringLiteral("EV charging platform admin server"));
    parser->addHelpOption();
    parser->addOption({QStringLiteral("server"), QStringLiteral("Run as headless server.")});
    parser->addOption({QStringLiteral("no-gui"), QStringLiteral("Run as headless server.")});
    parser->addOption({QStringLiteral("db"), QStringLiteral("SQLite database path."), QStringLiteral("path")});
    parser->addOption({QStringLiteral("host"), QStringLiteral("Listen host."), QStringLiteral("host"), QStringLiteral("127.0.0.1")});
    parser->addOption({QStringLiteral("port"), QStringLiteral("Listen port."), QStringLiteral("port"), QStringLiteral("9100")});
    parser->addOption({QStringLiteral("snapshot"), QStringLiteral("Dashboard snapshot output path."), QStringLiteral("path")});
}

AppContext::Options optionsFromParser(const QCommandLineParser &parser)
{
    AppContext::Options options;
    options.databasePath = parser.value(QStringLiteral("db"));
    options.host = parser.value(QStringLiteral("host"));
    bool portOk = false;
    const int port = parser.value(QStringLiteral("port")).toInt(&portOk);
    options.port = portOk && port > 0 && port <= 65535 ? static_cast<quint16>(port) : 9100;
    options.snapshotPath = parser.value(QStringLiteral("snapshot"));
    return options;
}

void printStartup(const QString &mode,
                  const AppContext &context,
                  const AppContext::Options &options)
{
    QTextStream output(stdout);
    output << QStringLiteral("ev_admin_server mode=") << mode
           << QStringLiteral(" listening on ") << context.host() << QStringLiteral(":") << context.port()
           << QStringLiteral(", db=") << context.databasePath()
           << QStringLiteral(", snapshot=") << options.snapshotPath << Qt::endl;
}

} // namespace

int main(int argc, char *argv[])
{
    const bool headless = isHeadlessRequested(argc, argv);
    if (headless) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("ChargingPlatformServer"));
        QCoreApplication::setOrganizationName(QStringLiteral("NeusoftTraining"));

        QCommandLineParser parser;
        configureParser(&parser);
        parser.process(app);

        const AppContext::Options options = optionsFromParser(parser);
        AppContext context;
        const Result initResult = context.initialize(options);
        QTextStream error(stderr);
        if (!initResult.ok) {
            error << QStringLiteral("启动失败：") << initResult.message << Qt::endl;
            return 1;
        }

        printStartup(QStringLiteral("headless"), context, options);
        return app.exec();
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ChargingPlatformServer"));
    QApplication::setOrganizationName(QStringLiteral("NeusoftTraining"));

    QCommandLineParser parser;
    configureParser(&parser);
    parser.process(app);

    const AppContext::Options options = optionsFromParser(parser);
    AppContext context;
    const Result initResult = context.initialize(options);
    if (!initResult.ok) {
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"), initResult.message);
        return 1;
    }

    printStartup(QStringLiteral("gui"), context, options);

    LoginDialog login(context.authService());
    if (login.exec() != QDialog::Accepted) {
        return 0;
    }

    MainWindow window(&context);
    window.show();
    return app.exec();
}
