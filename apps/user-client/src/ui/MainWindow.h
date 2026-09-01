#pragma once

#include "app/UserAppConfig.h"

#include <QMainWindow>

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(UserAppConfig config, QWidget *parent = nullptr);
};
