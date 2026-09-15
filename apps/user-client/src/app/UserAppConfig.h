#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

class UserAppConfig final {
public:
    QString serverHost;
    quint16 serverPort = 0;
    QString tencentMapKey;
    QStringList validationErrors;

    static UserAppConfig load(const QString &iniPath = {});

    // 内置默认腾讯地图 Key：env 与 config.local.ini 均未配置时的兜底值，
    // 使成员克隆仓库后零配置即可使用地图。env/ini 仍可覆盖（测试/换 Key 用）。
    [[nodiscard]] static QString bundledTencentMapKey();

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString validationMessage() const;
};
