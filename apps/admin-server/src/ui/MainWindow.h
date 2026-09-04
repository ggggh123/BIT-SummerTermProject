#pragma once

#include "app/AppContext.h"

#include <QMainWindow>
#include <QList>
#include <QStringList>

class QLabel;
class QTableWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppContext *context, QWidget *parent = nullptr);

private:
    QWidget *createDashboardPage();
    QWidget *createPileStatusPage();
    QWidget *createPlaceholderTablePage(const QStringList &headers, const QList<QStringList> &rows);
    QLabel *metricLabel(const QString &title, const QString &value);

    AppContext *m_context = nullptr;
};

