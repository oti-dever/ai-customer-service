#ifndef ROBOTSANDBOXMESSAGEDAO_H
#define ROBOTSANDBOXMESSAGEDAO_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct RobotSandboxMessageRecord
{
    qint64 id = 0;
    QString robotId;
    QString role;
    QString content;
    QString contentType = QStringLiteral("text");
    QString imagePath;
    QJsonObject metadata;
    QDateTime createdAt;
};

class RobotSandboxMessageDao
{
public:
    QVector<RobotSandboxMessageRecord> listForRobot(const QString& robotId, int limit = 200) const;
    QVector<RobotSandboxMessageRecord> listRecent(const QString& robotId, int limit = 10) const;
    bool append(const RobotSandboxMessageRecord& message);
    bool appendMany(const QVector<RobotSandboxMessageRecord>& messages);
    bool clearForRobot(const QString& robotId);
};

#endif // ROBOTSANDBOXMESSAGEDAO_H
