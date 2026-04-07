#ifndef MESSAGESENDEVENTDAO_H
#define MESSAGESENDEVENTDAO_H

#include <QDateTime>
#include <QString>
#include <QVector>

struct MessageSendEventRecord {
    qint64 id = 0;
    int messageId = 0;
    int conversationId = 0;
    QString phase;
    QString detail;
    QDateTime createdAt;
};

class MessageSendEventDao
{
public:
    MessageSendEventDao() = default;

    /** Max event id in table (for UI baseline after open session / clear). */
    qint64 globalMaxId() const;

    /** Events for one conversation with id strictly greater than afterId, oldest first. */
    QVector<MessageSendEventRecord> listSince(int conversationId, qint64 afterId, int limit = 200) const;
};

#endif // MESSAGESENDEVENTDAO_H
