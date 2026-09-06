#include "app/AppContext.h"
#include "core/BusinessTime.h"
#include "protocol/JsonEnvelope.h"
#include "contracts/Actions.h"
#include <QDir>
#include <QCoreApplication>

AppContext::AppContext(QObject *parent) : QObject(parent)
{
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &AppContext::shutdown);
}
AppContext::~AppContext() { shutdown(); }
Result AppContext::initialize() { return initialize(Options{}); }
Result AppContext::initialize(const Options &options)
{
    if (m_worker) return Result::failure("INTERNAL_ERROR","服务已经初始化");
    if (options.goldenPath.isEmpty()!=options.goldenHash.isEmpty())
        return Result::failure("INVALID_REQUEST","--golden 与 --golden-hash 必须配对提供");
    const QHostAddress address(options.host);
    if (address.isNull()) return Result::failure("NETWORK_ERROR","监听地址无效");
    m_worker = new DatabaseWorker;
    m_worker->moveToThread(&m_databaseThread);
    connect(&m_databaseThread,&QThread::finished,m_worker,&QObject::deleteLater);
    connect(m_worker,&DatabaseWorker::completed,this,&AppContext::deliver);
    connect(m_worker,&DatabaseWorker::healthChanged,this,[this](const auto &health) { m_health = health; });
    m_databaseThread.setObjectName("DatabaseWorker");
    m_databaseThread.start();
    Result initialized;
    const auto snapshot = options.snapshotPath.trimmed().isEmpty()
        ? QDir(QStringLiteral(EV_PROJECT_SOURCE_DIR)).filePath("dashboard/runtime/dashboard_snapshot.json")
        : options.snapshotPath;
    // 唯一同步屏障只用于启动；运行期本地请求全部排队。
    QMetaObject::invokeMethod(m_worker,[&] {
        initialized = m_worker->start(options.databasePath,snapshot,options.goldenPath,options.goldenHash);
        if (initialized.ok) { m_databasePath = m_worker->databasePath(); m_health = m_worker->health(); }
    },Qt::BlockingQueuedConnection);
    if (!initialized.ok) { shutdown(); return initialized; }
    m_apiServer = std::make_unique<ApiServer>();
    connect(m_apiServer.get(),&ApiServer::requestReceived,this,[this](ConnectionWorker *connection, const auto &request) {
        executeLocal(request,connection,[connection](const QByteArray &response) { connection->reply(response); });
    });
    if (!m_apiServer->listen(address,options.port)) {
        const auto error = m_apiServer->errorString(); shutdown();
        return Result::failure("NETWORK_ERROR",error);
    }
    m_host = options.host;
    m_port = m_apiServer->serverPort();
    m_accepting->store(true);
    return Result::success();
}
quint64 AppContext::enqueue(QObject *receiver, std::function<void(QByteArray)> callback)
{
    const auto sequence = ++m_sequence;
    m_pending.insert(sequence,{receiver,std::move(callback)});
    return sequence;
}
void AppContext::deliver(quint64 sequence, const QByteArray &bytes)
{
    if (!m_pending.contains(sequence)) return;
    auto pending = m_pending.take(sequence);
    if (!pending.receiver) return;
    const auto accepting = m_accepting;
    QMetaObject::invokeMethod(pending.receiver,[accepting,callback=std::move(pending.callback),bytes] {
        if (accepting->load()) callback(bytes);
    },Qt::QueuedConnection);
}
void AppContext::executeLocal(ev::protocol::RequestEnvelope request, QObject *receiver, std::function<void(QByteArray)> callback)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_accepting->load() || !receiver) return;
    const auto sequence = enqueue(receiver,std::move(callback));
    try { request = ev::protocol::parseRequest(ev::protocol::toJson(request)); }
    catch (const ev::protocol::EnvelopeError &error) {
        deliver(sequence,ev::protocol::toJson({request.requestId,false,error.code(),error.message(),QJsonObject{}})); return;
    }
    if (!ev::actions::all().contains(request.action)) {
        deliver(sequence,ev::protocol::toJson({request.requestId,false,"INVALID_REQUEST","未知接口动作",QJsonObject{}})); return;
    }
    if (request.action == ev::actions::SystemHealth) {
        deliver(sequence,ev::protocol::toJson({request.requestId,true,"OK","ready",healthSnapshot()})); return;
    }
    if (m_pending.size() > 256) {
        deliver(sequence,ev::protocol::toJson({request.requestId,false,"SERVER_BUSY","请求队列已满",QJsonObject{}})); return;
    }
    QMetaObject::invokeMethod(m_worker,[worker=m_worker,sequence,request] { worker->execute(sequence,request); },Qt::QueuedConnection);
}
void AppContext::queryAdmin(AdminView view, const QString &token, const QJsonObject &parameters,
                            QObject *receiver, std::function<void(QByteArray)> callback)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_accepting->load() || !receiver) return;
    const auto sequence = enqueue(receiver,std::move(callback));
    if (m_pending.size() > 256) {
        deliver(sequence,ev::protocol::toJson({{},false,"SERVER_BUSY","请求队列已满",QJsonObject{}})); return;
    }
    QMetaObject::invokeMethod(m_worker,[worker=m_worker,sequence,view,token,parameters] {
        worker->query(sequence,view,token,parameters);
    },Qt::QueuedConnection);
}
void AppContext::shutdown()
{
    m_accepting->store(false);
    if (m_apiServer) m_apiServer->stop();
    m_pending.clear();
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker,&DatabaseWorker::stop,Qt::QueuedConnection);
        m_databaseThread.wait();
        m_worker = nullptr;
    }
}
QJsonObject AppContext::healthSnapshot() const
{
    auto health = m_health;
    health.insert("serverTime",BusinessTime::now());
    return health;
}
ApiServer *AppContext::apiServer() const { return m_apiServer.get(); }
QString AppContext::databasePath() const { return m_databasePath; }
QString AppContext::host() const { return m_host; }
quint16 AppContext::port() const { return m_port; }
