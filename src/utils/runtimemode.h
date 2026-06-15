#ifndef RUNTIMEMODE_H
#define RUNTIMEMODE_H

#include "appsettings.h"

#include <QSettings>
#include <QString>

namespace RuntimeMode {

inline QString singleHostServiceDb()
{
    return QStringLiteral("single_host_service_db");
}

inline QString clientCacheDb()
{
    return QStringLiteral("client_cache_db");
}

inline QString remoteService()
{
    return QStringLiteral("remote_service");
}

inline QString defaultMode()
{
    return singleHostServiceDb();
}

inline QString currentMode()
{
    QSettings settings = AppSettings::create();
    QString mode = settings.value(QStringLiteral("runtime/mode"), defaultMode()).toString().trimmed();
    if (mode == clientCacheDb() || mode == remoteService() || mode == singleHostServiceDb())
        return mode;
    return defaultMode();
}

inline void setCurrentMode(const QString& mode)
{
    const QString normalized = mode.trimmed();
    QSettings settings = AppSettings::create();
    if (normalized == clientCacheDb() || normalized == remoteService() || normalized == singleHostServiceDb())
        settings.setValue(QStringLiteral("runtime/mode"), normalized);
    else
        settings.setValue(QStringLiteral("runtime/mode"), defaultMode());
}

inline bool isSingleHostServiceDb()
{
    return currentMode() == singleHostServiceDb();
}

inline bool isRemoteService()
{
    return currentMode() == remoteService();
}

inline bool isClientCacheDb()
{
    return currentMode() == clientCacheDb();
}

inline bool ownsBusinessDatabase()
{
    return isSingleHostServiceDb() || isRemoteService();
}

} // namespace RuntimeMode

#endif // RUNTIMEMODE_H
