#ifndef AIASSISTANTDAO_H
#define AIASSISTANTDAO_H

#include <QString>
#include <QVector>

struct AiAssistantChatTurn
{
    QString role;
    QString content;
};

class AiAssistantDao
{
public:
    AiAssistantDao() = default;

    /** 与文档 §10.3 一致：由 Base URL 主机 + 模型名构成稳定槽位键（单配置源时尚无下拉 preset）。 */
    static QString modelKeyFromBaseUrlAndModel(const QString& baseUrl, const QString& model);

    /** 取或创建 (user_id, model_key) 对应会话行，返回 session id；-1 表示失败。 */
    int ensureSession(int userId, const QString& modelKey);

    QVector<AiAssistantChatTurn> listMessages(int sessionId) const;
    bool appendMessage(int sessionId, const QString& role, const QString& content);
    /** 物理删除该会话下全部消息，保留 sessions 行（§10.6）。 */
    bool clearMessages(int sessionId);
};

#endif
