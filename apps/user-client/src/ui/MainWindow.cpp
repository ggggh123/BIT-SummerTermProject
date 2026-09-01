#include "ui/MainWindow.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(UserAppConfig config, QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("电动汽车充电"));

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);

    auto *configurationMessage = new QLabel(centralWidget);
    configurationMessage->setObjectName(QStringLiteral("configurationMessage"));
    configurationMessage->setWordWrap(true);
    if (config.isValid()) {
        configurationMessage->setText(QStringLiteral("配置已就绪"));
    } else {
        configurationMessage->setText(QStringLiteral("配置错误：\n%1").arg(config.validationMessage()));
    }
    layout->addWidget(configurationMessage);

    auto *pages = new QStackedWidget(centralWidget);
    pages->setObjectName(QStringLiteral("mainPages"));
    auto *loginPlaceholder = new QLabel(QStringLiteral("登录页面尚未实现"), pages);
    loginPlaceholder->setAlignment(Qt::AlignCenter);
    pages->addWidget(loginPlaceholder);
    layout->addWidget(pages);

    setCentralWidget(centralWidget);
}
