#ifndef QIANNIUCONVERSATIONDAO_H
#define QIANNIUCONVERSATIONDAO_H

#include <QJsonObject>
#include <QString>

class QianniuConversationDao
{
public:
    bool upsertConversation(int conversationId,
                            const QString& accountId,
                            const QString& conversationKey,
                            const QString& displayName,
                            const QJsonObject& payload = QJsonObject());

    bool createMessageExtension(int messageId,
                                int conversationId,
                                const QString& accountId,
                                const QString& conversationKey,
                                const QString& displayName,
                                const QString& platformMsgId,
                                const QJsonObject& payload);

    bool deleteForConversation(int conversationId);
};

#endif // QIANNIUCONVERSATIONDAO_H
