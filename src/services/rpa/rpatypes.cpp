#include "rpatypes.h"

namespace Rpa {

QString toString(PlatformId id)
{
    switch (id) {
    case PlatformId::Wechat: return QStringLiteral("wechat");
    case PlatformId::Qianniu: return QStringLiteral("qianniu");
    case PlatformId::Unknown:
    default: return QStringLiteral("unknown");
    }
}

QString toString(ProcessState state)
{
    switch (state) {
    case ProcessState::Stopped: return QStringLiteral("stopped");
    case ProcessState::Starting: return QStringLiteral("starting");
    case ProcessState::Running: return QStringLiteral("running");
    case ProcessState::Stopping: return QStringLiteral("stopping");
    case ProcessState::Error: return QStringLiteral("error");
    default: return QStringLiteral("unknown");
    }
}

PlatformId platformIdFromString(const QString& value)
{
    if (value == QLatin1String("wechat")) return PlatformId::Wechat;
    if (value == QLatin1String("qianniu")) return PlatformId::Qianniu;
    return PlatformId::Unknown;
}

ProcessState processStateFromString(const QString& value)
{
    if (value == QLatin1String("stopped")) return ProcessState::Stopped;
    if (value == QLatin1String("starting")) return ProcessState::Starting;
    if (value == QLatin1String("running")) return ProcessState::Running;
    if (value == QLatin1String("stopping")) return ProcessState::Stopping;
    if (value == QLatin1String("error")) return ProcessState::Error;
    return ProcessState::Stopped;
}

QStringList allPlatformIds()
{
    return {
        QStringLiteral("wechat"),
        QStringLiteral("qianniu"),
    };
}

} // namespace Rpa
