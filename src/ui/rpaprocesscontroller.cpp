#include "rpaprocesscontroller.h"
#include "../services/rpa/rpaservice.h"

RpaProcessController::RpaProcessController(QObject* parent)
    : QObject(parent)
    , m_service(&Rpa::RpaService::instance())
{
    connect(m_service, &Rpa::RpaService::platformLogAppended,
            this, &RpaProcessController::logAppended);
    connect(m_service, &Rpa::RpaService::statusMessage,
            this, &RpaProcessController::statusMessageRequested);
}

QString RpaProcessController::processLog(const QString& platformId) const
{
    return m_service->platformLog(platformId);
}

void RpaProcessController::clearProcessLog(const QString& platformId)
{
    m_service->clearPlatformLog(platformId);
}

QStringList RpaProcessController::runningPlatformIds() const
{
    return m_service->runningPlatforms();
}

void RpaProcessController::startPlatforms(const QStringList& platformIds)
{
    for (const QString& id : platformIds) {
        m_service->startPlatform(id);
    }
}

void RpaProcessController::stopPlatforms(const QStringList& platformIds)
{
    for (const QString& id : platformIds) {
        m_service->stopPlatform(id);
    }
}
