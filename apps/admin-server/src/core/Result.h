#pragma once

#include <QString>

struct Result
{
    bool ok = false;
    QString code;
    QString message;

    static Result success(const QString &message = QStringLiteral("success"))
    {
        return {true, QStringLiteral("OK"), message};
    }

    static Result failure(const QString &code, const QString &message)
    {
        return {false, code, message};
    }
};

