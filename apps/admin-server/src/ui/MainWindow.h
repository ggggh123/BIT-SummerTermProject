#pragma once

#include "app/AppContext.h"

#include <QMainWindow>
#include <QHash>
#include <QList>
#include <QStringList>

#include <functional>

class QLabel;
class QTableWidget;
class QTabWidget;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppContext *context, const QString &adminToken, QWidget *parent = nullptr);

private:
    QWidget *createDashboardPage();
    QWidget *createPileStatusPage();
    QWidget *createChargerManagementPage();
    QWidget *createStationManagementPage();
    QWidget *createUserManagementPage();
    QWidget *createRequestLogPage();
    QWidget *createHealthPage();
    QWidget *createPlaceholderTablePage(const QStringList &headers, const QList<QStringList> &rows);
    QLabel *metricLabel(const QString &title, const QString &value);
    void registerPageRefresh(QWidget *page, std::function<void()> refresh);
    void refreshCurrentPage();
    void queryView(AdminView view, const QJsonObject &parameters, QObject *receiver,
                   std::function<void(QJsonObject)> callback);
    void queryRows(AdminView view, const QJsonObject &parameters, QTableWidget *table);
    void mutate(const QString &action, const QJsonObject &payload, const QList<QWidget *> &controls,
                std::function<void()> callback);

    AppContext *m_context = nullptr;
    QString m_adminToken;
    QTabWidget *m_tabs = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QHash<QWidget *, std::function<void()>> m_pageRefreshers;
};
