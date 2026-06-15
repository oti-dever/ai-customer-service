#ifndef AIREQUESTEVENTDAO_H
#define AIREQUESTEVENTDAO_H

#include <QDateTime>
#include <QString>
#include <QVector>

struct AiRequestEventMetrics {
    int todayRequestCount = 0;
    int successRatePercent = 0;
    bool hasSuccessRate = false;
    int averageDurationMs = 0;
    bool hasAverageDuration = false;
    QString latestStatus;
    QString latestError;
};

struct AiRequestStageEventRecord {
    qint64 id = 0;
    qint64 requestEventId = 0;
    int conversationId = 0;
    QString stage;
    QString detail;
    QDateTime createdAt;
};

class AiRequestEventDao
{
public:
    AiRequestEventDao() = default;

    qint64 beginEvent(const QString& source,
                      int conversationId,
                      const QString& sessionModelKey,
                      const QString& model,
                      const QString& triggerTag);
    bool completeEvent(qint64 eventId, int durationMs, int firstTokenMs, int outputChars);
    bool failEvent(qint64 eventId, int durationMs, const QString& errorReason);
    bool cancelEvent(qint64 eventId, int durationMs);
    bool appendStage(qint64 requestEventId,
                     int conversationId,
                     const QString& stage,
                     const QString& detail = QString());

    AiRequestEventMetrics aggregateMetrics(const QString& sessionModelKey) const;
    qint64 globalStageMaxId() const;
    QVector<AiRequestStageEventRecord> listStagesSince(int conversationId,
                                                       qint64 afterId,
                                                       int limit = 200) const;
};

#endif // AIREQUESTEVENTDAO_H
