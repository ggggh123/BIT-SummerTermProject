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
    explicit MainWindow(AppContext *context, QWidget *parent = nullptr);

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

    AppContext *m_context = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QHash<QWidget *, std::function<void()>> m_pageRefreshers;
};
