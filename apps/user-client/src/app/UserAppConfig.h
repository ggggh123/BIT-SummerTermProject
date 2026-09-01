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

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString validationMessage() const;
};
