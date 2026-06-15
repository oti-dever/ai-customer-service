#ifndef CUSTOMERPROFILEDAO_H
#define CUSTOMERPROFILEDAO_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <optional>

struct CustomerProfileRecord {
    int conversationId = 0;
    QJsonObject profile;
    QString sourceModelKey;
    qint64 sourceRequestEventId = 0;
    QDateTime updatedAt;
};

class CustomerProfileDao
{
public:
    CustomerProfileDao() = default;

    std::optional<CustomerProfileRecord> findByConversationId(int conversationId) const;
    bool upsert(int conversationId,
                const QJsonObject& profile,
                const QString& sourceModelKey,
                qint64 sourceRequestEventId);
    bool remove(int conversationId);
};

#endif // CUSTOMERPROFILEDAO_H
