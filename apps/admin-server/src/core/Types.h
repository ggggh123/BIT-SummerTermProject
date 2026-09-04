#pragma once

#include <QString>

enum class PileStatus
{
    Idle,
    Using,
    Fault
};

inline QString pileStatusText(PileStatus status)
{
    switch (status) {
    case PileStatus::Idle:
        return QStringLiteral("闲置");
    case PileStatus::Using:
        return QStringLiteral("在用");
    case PileStatus::Fault:
        return QStringLiteral("故障");
    }
    return QStringLiteral("未知");
}

