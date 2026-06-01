#ifndef MESSAGELISTMODEL_H
#define MESSAGELISTMODEL_H

#include "../core/types.h"
#include "../models/unifiedmodels.h"
#include <QAbstractListModel>
#include <QVector>

class MessageListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        MessageIdRole = Qt::UserRole + 1,
        IsSeparatorRole,
        SeparatorDateRole,
        MessageRole,
        MessageStatusRole,
    };

    explicit MessageListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void setConversationMessages(int conversationId, const QVector<MessageRecord>& messages);
    void clear();
    void appendMessage(const MessageRecord& message);

    bool updateMessageStatus(int messageId, Models::MessageStatus newStatus, const QString& errorReason = QString());
    bool updateMessageById(int messageId, const MessageRecord& updatedMessage);

    int conversationId() const;
    QVector<MessageRecord> messages() const;
    QString signature() const;

    int findRowByMessageId(int messageId) const;
    bool containsMessageId(int messageId) const;

signals:
    void messageStatusChanged(int messageId, Models::MessageStatus newStatus);

private:
    struct Row {
        bool separator = false;
        QDate separatorDate;
        MessageRecord message;
    };

    void rebuildRows();
    int findMessageIndex(int messageId) const;

    int m_conversationId = -1;
    QVector<MessageRecord> m_messages;
    QVector<Row> m_rows;
};

#endif // MESSAGELISTMODEL_H
