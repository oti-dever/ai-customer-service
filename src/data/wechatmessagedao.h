#ifndef WECHATMESSAGEDAO_H
#define WECHATMESSAGEDAO_H

#include <QJsonObject>
#include <QString>

class WechatMessageDao
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

#endif // WECHATMESSAGEDAO_H
