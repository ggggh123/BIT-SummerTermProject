#include "db/DatabaseWorker.h"
#include "db/DatabaseManager.h"
#include "services/RequestDispatcher.h"
#include "protocol/JsonEnvelope.h"
#include "contracts/Actions.h"
#include <QJsonArray>
#include <QThread>
#include <QTimer>
#include <QSqlQuery>

struct DatabaseWorker::State {
    DatabaseManager database;
    std::unique_ptr<AuthService> auth;
    std::unique_ptr<AdminService> admin;
    std::unique_ptr<DashboardService> dashboard;
    std::unique_ptr<ForecastService> forecast;
    std::unique_ptr<RequestLogService> log;
    std::unique_ptr<TelemetryService> telemetry;
    std::unique_ptr<UserService> user;
    std::unique_ptr<RequestDispatcher> dispatcher;
};
DatabaseWorker::DatabaseWorker() = default;
DatabaseWorker::~DatabaseWorker() = default;

Result DatabaseWorker::start(const QString &path, const QString &snapshot)
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_state = std::make_unique<State>();
    auto &s = *m_state;
    const auto opened = s.database.open(path);
    if (!opened.ok) return opened;
    s.auth = std::make_unique<AuthService>(s.database.database());
    s.admin = std::make_unique<AdminService>(s.database.database());
    s.dashboard = std::make_unique<DashboardService>(s.database.database());
    s.forecast = std::make_unique<ForecastService>(s.database.database(), snapshot);
    s.log = std::make_unique<RequestLogService>(s.database.database());
    s.telemetry = std::make_unique<TelemetryService>(s.database.database());
    s.user = std::make_unique<UserService>(s.database.database());
    const auto schema = s.log->ensureSchema();
    if (!schema.ok) return schema;
    s.dispatcher = std::make_unique<RequestDispatcher>(s.auth.get(), s.admin.get(), s.dashboard.get(),
        s.forecast.get(), s.log.get(), s.telemetry.get(), s.user.get());
    refreshHealth();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        QMetaObject::invokeMethod(this, [this] { if (!m_stopping) refreshHealth(); }, Qt::QueuedConnection);
    });
    timer->start(1000);
    // 上次进程停止时已提交的重启，在新进程中继续确定性完成。
    QSqlQuery pending(s.database.database());
    if (pending.exec(QStringLiteral("SELECT id FROM chargers WHERE status='restarting'")))
        while (pending.next()) scheduleRestart(pending.value(0).toInt());
    return Result::success();
}
QString DatabaseWorker::databasePath() const { return m_state->database.databasePath(); }
QJsonObject DatabaseWorker::health() const { return m_health; }
void DatabaseWorker::refreshHealth()
{
    m_health = m_state->forecast->healthState();
    emit healthChanged(m_health);
}
void DatabaseWorker::execute(quint64 sequence, const ev::protocol::RequestEnvelope &request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto bytes = m_state->dispatcher->dispatch(request);
    if (request.action == ev::actions::AdminChargerRestart) {
        const auto response = ev::protocol::parseResponse(bytes);
        if (response.ok) scheduleRestart(request.payload.value(QStringLiteral("chargerId")).toInt());
    }
    refreshHealth();
    emit completed(sequence, bytes);
}
void DatabaseWorker::query(quint64 sequence, AdminView view, const QString &token, const QJsonObject &p)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto &s = *m_state;
    if (!s.auth->isTokenValid(token)) {
        const bool known = s.auth->isUserTokenValid(token) || s.auth->isSimulatorTokenValid(token) || s.auth->isMlTokenValid(token);
        emit completed(sequence, ev::protocol::toJson({{},false,known ? QStringLiteral("FORBIDDEN") : QStringLiteral("AUTH_REQUIRED"),
            QStringLiteral("管理视图需要管理员身份"), QJsonObject{}}));
        return;
    }
    QJsonObject data;
    QList<QStringList> rows;
    Result result = Result::success();
    switch (view) {
    case AdminView::Summary: data = s.dashboard->summary(p.value("rangeDays").toInt(7)); break;
    case AdminView::Stations: rows = s.dashboard->stationRows(); break;
    case AdminView::Chargers: rows = s.dashboard->chargerRows(p.value("stationId").toInt(), p.value("status").toString()); break;
    case AdminView::Users: rows = s.dashboard->userRows(p.value("mobileLike").toString(), p.value("limit").toInt(20), p.value("offset").toInt()); break;
    case AdminView::RequestLog: result = s.log->list({},50,0,&data); break;
    default: result = Result::failure("INVALID_REQUEST", "未知管理视图"); break;
    }
    QJsonArray array;
    for (const auto &row : rows) array.append(QJsonArray::fromStringList(row));
    data.insert("rows", array);
    emit completed(sequence, ev::protocol::toJson({{},result.ok,result.ok ? QStringLiteral("OK") : result.code,
        result.message,result.ok ? data : QJsonObject{}}));
}
void DatabaseWorker::scheduleRestart(int id)
{
    if (m_restarts.contains(id)) return;
    QSqlQuery state(m_state->database.database());
    state.prepare(QStringLiteral("SELECT status FROM chargers WHERE id=?"));
    state.addBindValue(id);
    if (!state.exec() || !state.next() || state.value(0).toString() != QStringLiteral("restarting")) return;
    m_restarts.insert(id);
    QTimer::singleShot(1500, Qt::PreciseTimer, this, [this,id] {
        // Timer 只排队；完成事务仍由串行命令队列执行。
        QMetaObject::invokeMethod(this, [this,id] {
            const auto result = m_state->admin->finishRestart(id);
            if (!result.ok) qWarning("charger restart completion failed: %s", qPrintable(result.code));
            m_restarts.remove(id);
            refreshHealth();
            if (m_stopping && m_restarts.isEmpty()) finishStop();
        }, Qt::QueuedConnection);
    });
}
void DatabaseWorker::stop()
{
    m_stopping = true;
    for (auto *timer : findChildren<QTimer *>()) timer->stop();
    if (m_restarts.isEmpty()) finishStop();
}
void DatabaseWorker::finishStop()
{
    m_state.reset(); // 服务的所有 QSqlDatabase 副本先销毁，然后关闭并移除命名连接。
    thread()->quit();
}
