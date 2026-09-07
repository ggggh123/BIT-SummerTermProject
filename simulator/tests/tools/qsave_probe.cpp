// qsave_probe.cpp — 诊断程序：验证 Qt 6.2.4 的 QSaveFile 在 RLIMIT_FSIZE=0 下是否上报写失败
// 对照组：裸 QFile。输出四行关键信息。
//
// 背景：调查报告 docs/test/simulator-runtime-status-suite-crash-2026-09-07.md §3/§5。
// 实测结论（Qt 6.2.4 / Ubuntu 22.04）：内核拒绝写入（EFBIG）时 QSaveFile::write()/
// commit() 仍返回成功，仅 errorString() 记录"文件过大"——写失败是静默的。
//
// 编译（在项目根目录）：
//   g++ -fPIC simulator/tests/tools/qsave_probe.cpp \
//       $(pkg-config --cflags --libs Qt6Core) -o /tmp/qsave_probe
// 运行：
//   /tmp/qsave_probe /tmp/qsave-probe.json
#include <QCoreApplication>
#include <QSaveFile>
#include <QFile>
#include <QDebug>
#include <csignal>
#include <sys/resource.h>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral("/tmp/qsave-probe.json");

    // 第一步：正常发布（模拟"已发布 ready"）
    {
        QSaveFile f(path);
        const bool okOpen = f.open(QIODevice::WriteOnly);
        const qint64 n = f.write(QByteArrayLiteral("{\"sessionState\":\"ready\"}"));
        const bool okCommit = f.commit();
        qInfo() << "[1] PUBLISH       open=" << okOpen << " write=" << n << " commit=" << okCommit;
    }

    // 第二步：复刻探针序列——忽略 SIGXFSZ，软限设 0
    std::signal(SIGXFSZ, SIG_IGN);
    struct rlimit limit;
    if (getrlimit(RLIMIT_FSIZE, &limit) != 0) {
        qWarning() << "getrlimit failed";
        return 1;
    }
    limit.rlim_cur = 0;
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
        qWarning() << "setrlimit failed";
        return 2;
    }

    // 第三步：QSaveFile 再写（探针期望这里失败）
    {
        QSaveFile f(path);
        const bool okOpen = f.open(QIODevice::WriteOnly);
        const qint64 n = okOpen ? f.write(QByteArrayLiteral("{\"sessionState\":\"refresh\"}")) : -2;
        const bool okCommit = f.commit();
        qInfo() << "[2] QSaveFile     open=" << okOpen << " write=" << n
                << " commit=" << okCommit << " error=" << f.errorString();
    }

    // 第四步：对照组——裸 QFile 写（应失败）
    {
        QFile f(path + QStringLiteral(".plain"));
        const bool okOpen = f.open(QIODevice::WriteOnly);
        const qint64 n = okOpen ? f.write(QByteArrayLiteral("{\"x\":1}")) : -2;
        qInfo() << "[3] PLAIN QFile   open=" << okOpen << " write=" << n
                << " error=" << f.errorString();
    }
    return 0;
}
