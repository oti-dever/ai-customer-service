#ifndef RUNTIMEMODE_H
#define RUNTIMEMODE_H

#include <QString>
#include <QtGlobal>

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
    return defaultMode();
}

inline void setCurrentMode(const QString& mode)
{
    Q_UNUSED(mode);
}

inline bool isSingleHostServiceDb()
{
    return currentMode() == singleHostServiceDb();
}

inline bool isRemoteService()
{
    return false;
}

inline bool isClientCacheDb()
{
    return false;
}

inline bool ownsBusinessDatabase()
{
    return true;
}

} // namespace RuntimeMode

#endif // RUNTIMEMODE_H
