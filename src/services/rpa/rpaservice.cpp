#include "rpaservice.h"
#include "rpaprocessmanager.h"

namespace Rpa {

RpaService& RpaService::instance()
{
    static RpaService s_instance;
    return s_instance;
}

RpaService::RpaService(QObject* parent)
    : QObject(parent)
    , m_manager(&RpaProcessManager::instance())
{
    connect(m_manager, &RpaProcessManager::platformStateChanged,
            this, [this](const QString& platformId, ProcessState newState) {
                emit platformStateChanged(platformId, newState);

                QString message;
                switch (newState) {
                case ProcessState::Running:
                    message = QStringLiteral("已启动 RPA：%1").arg(platformId);
                    break;
                case ProcessState::Stopped:
                    message = QStringLiteral("已停止 RPA：%1").arg(platformId);
                    break;
                case ProcessState::Error:
                    message = QStringLiteral("RPA 错误：%1").arg(platformId);
                    break;
                default:
                    break;
                }
                if (!message.isEmpty()) {
                    emit statusMessage(message, 3000);
                }
            });

    connect(m_manager, &RpaProcessManager::logAppended,
            this, &RpaService::platformLogAppended);

    connect(m_manager, &RpaProcessManager::errorOccurred,
            this, [this](const QString& platformId, const QString& error) {
                emit platformError(platformId, error);
                emit statusMessage(error, 5000);
            });
}

void RpaService::initialize()
{
    m_manager->initialize();
    qInfo() << "[RpaService] 初始化完成";
}

void RpaService::shutdown()
{
    m_manager->shutdown();
    qInfo() << "[RpaService] 已关闭";
}

PlatformStatus RpaService::platformStatus(const QString& platformId) const
{
    return m_manager->status(platformId);
}

QVector<PlatformStatus> RpaService::allPlatformStatuses() const
{
    return m_manager->allStatuses();
}

QStringList RpaService::runningPlatforms() const
{
    return m_manager->runningPlatformIds();
}

QString RpaService::platformLog(const QString& platformId) const
{
    return m_manager->processLog(platformId);
}

void RpaService::clearPlatformLog(const QString& platformId)
{
    m_manager->clearProcessLog(platformId);
}

void RpaService::startPlatform(const QString& platformId)
{
    m_manager->startPlatform(platformId);
}

void RpaService::stopPlatform(const QString& platformId)
{
    m_manager->stopPlatform(platformId);
}

void RpaService::startAllPlatforms()
{
    m_manager->startPlatforms(allPlatformIds());
}

void RpaService::stopAllPlatforms()
{
    m_manager->stopAll();
}

} // namespace Rpa
