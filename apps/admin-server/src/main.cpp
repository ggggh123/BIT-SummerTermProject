#include "app/AppContext.h"
#include "ui/LoginDialog.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>
#include <QSocketNotifier>
#ifdef Q_OS_UNIX
#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

#ifdef Q_OS_UNIX
volatile std::sig_atomic_t shutdownWriteFd = -1;
void shutdownSignal(int)
{
    const int savedErrno = errno;
    const char byte = 1;
    if (shutdownWriteFd >= 0) {
        const auto ignored = ::write(shutdownWriteFd,&byte,1);
        (void)ignored;
    }
    errno = savedErrno;
}
#endif

// POSIX handler 只写自管道，由 Qt 主事件循环发起正常关闭。
class ShutdownSignals {
public:
    explicit ShutdownSignals(QCoreApplication &app)
    {
#ifdef Q_OS_UNIX
        if (::pipe(m_fds) != 0) return;
        for (const int fd : m_fds) {
            ::fcntl(fd,F_SETFL,::fcntl(fd,F_GETFL) | O_NONBLOCK);
            ::fcntl(fd,F_SETFD,FD_CLOEXEC);
        }
        shutdownWriteFd = m_fds[1];
        m_notifier = new QSocketNotifier(m_fds[0],QSocketNotifier::Read,&app);
        QObject::connect(m_notifier,&QSocketNotifier::activated,&app,[this,&app] {
            char bytes[32];
            while (::read(m_fds[0],bytes,sizeof(bytes)) > 0) {}
            // 登录对话框可能正在 app.exec() 之前的局部事件循环中。
            if (auto *gui = qobject_cast<QApplication *>(&app)) gui->closeAllWindows();
            app.quit();
        });
        m_oldTerm = std::signal(SIGTERM,shutdownSignal);
        m_oldInt = std::signal(SIGINT,shutdownSignal);
#else
        Q_UNUSED(app)
#endif
    }
    ~ShutdownSignals()
    {
#ifdef Q_OS_UNIX
        if (m_fds[0] < 0) return;
        std::signal(SIGTERM,m_oldTerm);
        std::signal(SIGINT,m_oldInt);
        shutdownWriteFd = -1;
        delete m_notifier;
        ::close(m_fds[0]); ::close(m_fds[1]);
#endif
    }
private:
#ifdef Q_OS_UNIX
    int m_fds[2] = {-1,-1};
    QSocketNotifier *m_notifier = nullptr;
    using Handler = void (*)(int);
    Handler m_oldTerm = SIG_DFL;
    Handler m_oldInt = SIG_DFL;
#endif
};

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
    parser->addOption({QStringLiteral("golden"), QStringLiteral("Approved golden SQLite input path (requires --golden-hash)."), QStringLiteral("path")});
    parser->addOption({QStringLiteral("golden-hash"), QStringLiteral("Approved lowercase SHA-256 (requires --golden)."), QStringLiteral("sha256")});
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
    options.goldenPath = parser.value(QStringLiteral("golden"));
    options.goldenHash = parser.value(QStringLiteral("golden-hash"));
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
        ShutdownSignals shutdownSignals(app);
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
    ShutdownSignals shutdownSignals(app);
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

    LoginDialog login(&context);
    if (login.exec() != QDialog::Accepted) {
        return 0;
    }

    MainWindow window(&context, login.adminToken());
    window.show();
    return app.exec();
}
