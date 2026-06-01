#include "messagelistmodel.h"

#include <QSet>
#include <QStringList>

MessageListModel::MessageListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MessageListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant MessageListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row& row = m_rows.at(index.row());
    if (row.separator) {
        switch (role) {
        case IsSeparatorRole:
            return true;
        case SeparatorDateRole:
            return row.separatorDate;
        case Qt::DisplayRole:
            return row.separatorDate.toString(QStringLiteral("yyyy-MM-dd"));
        default:
            return {};
        }
    }

    switch (role) {
    case IsSeparatorRole:
        return false;
    case MessageIdRole:
        return row.message.id;
    case MessageRole:
        return QVariant::fromValue(row.message);
    case MessageStatusRole:
        return row.message.status;
    case Qt::DisplayRole:
        return row.message.content;
    default:
        return {};
    }
}

void MessageListModel::setConversationMessages(int conversationId,
                                               const QVector<MessageRecord>& messages)
{
    beginResetModel();
    m_conversationId = conversationId;
    m_messages.clear();
    m_messages.reserve(messages.size());
    QSet<int> seenIds;
    for (const MessageRecord& message : messages) {
        if (message.id > 0) {
            if (seenIds.contains(message.id))
                continue;
            seenIds.insert(message.id);
        }
        m_messages.push_back(message);
    }
    rebuildRows();
    endResetModel();
}

void MessageListModel::clear()
{
    setConversationMessages(-1, {});
}

void MessageListModel::appendMessage(const MessageRecord& message)
{
    if (message.id > 0 && containsMessageId(message.id))
        return;

    m_conversationId = message.conversationId;
    QDate lastMsgDate;
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (m_rows[i].separator)
            continue;
        lastMsgDate = m_rows[i].message.createdAt.isValid()
            ? m_rows[i].message.createdAt.date()
            : QDate::currentDate();
        break;
    }
    const QDate msgDate = message.createdAt.isValid() ? message.createdAt.date() : QDate::currentDate();
    const bool needsSeparator = !lastMsgDate.isValid() || msgDate != lastMsgDate;
    const int first = m_rows.size();
    const int last = first + (needsSeparator ? 1 : 0);
    beginInsertRows(QModelIndex(), first, last);
    m_messages.push_back(message);
    if (needsSeparator) {
        m_rows.push_back(Row{true, msgDate, {}});
    }
    m_rows.push_back(Row{false, {}, message});
    endInsertRows();
}

bool MessageListModel::containsMessageId(int messageId) const
{
    return messageId > 0 && findMessageIndex(messageId) >= 0;
}

int MessageListModel::findMessageIndex(int messageId) const
{
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].id == messageId)
            return i;
    }
    return -1;
}

int MessageListModel::findRowByMessageId(int messageId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!m_rows[i].separator && m_rows[i].message.id == messageId)
            return i;
    }
    return -1;
}

bool MessageListModel::updateMessageStatus(int messageId, Models::MessageStatus newStatus, const QString& errorReason)
{
    int msgIdx = findMessageIndex(messageId);
    if (msgIdx < 0)
        return false;

    m_messages[msgIdx].status = Models::toString(newStatus);
    m_messages[msgIdx].syncStatus = Models::legacySyncStatusFromMessageStatus(newStatus);
    if (!errorReason.isEmpty())
        m_messages[msgIdx].errorReason = errorReason;

    int rowIdx = findRowByMessageId(messageId);
    if (rowIdx >= 0) {
        m_rows[rowIdx].message = m_messages[msgIdx];
        QModelIndex idx = index(rowIdx);
        emit dataChanged(idx, idx, {MessageRole, MessageStatusRole});
    }

    emit messageStatusChanged(messageId, newStatus);
    return true;
}

bool MessageListModel::updateMessageById(int messageId, const MessageRecord& updatedMessage)
{
    int msgIdx = findMessageIndex(messageId);
    if (msgIdx < 0)
        return false;

    m_messages[msgIdx] = updatedMessage;

    int rowIdx = findRowByMessageId(messageId);
    if (rowIdx >= 0) {
        m_rows[rowIdx].message = updatedMessage;
        QModelIndex idx = index(rowIdx);
        emit dataChanged(idx, idx, {MessageRole, MessageStatusRole});
    }
    return true;
}

int MessageListModel::conversationId() const
{
    return m_conversationId;
}

QVector<MessageRecord> MessageListModel::messages() const
{
    return m_messages;
}

QString MessageListModel::signature() const
{
    QStringList parts;
    parts.reserve(m_messages.size());
    for (const MessageRecord& msg : m_messages) {
        parts.append(QStringList{
                         QString::number(msg.id),
                         QString::number(msg.syncStatus),
                         msg.errorReason,
                         msg.content,
                         msg.contentImagePath,
                         msg.originalTimestamp,
                     }.join(QChar(0x1f)));
    }
    return parts.join(QChar('|'));
}

void MessageListModel::rebuildRows()
{
    m_rows.clear();
    QDate lastDate;
    for (const MessageRecord& msg : m_messages) {
        const QDate msgDate = msg.createdAt.isValid() ? msg.createdAt.date() : QDate::currentDate();
        if (!lastDate.isValid() || msgDate != lastDate) {
            m_rows.push_back(Row{true, msgDate, {}});
            lastDate = msgDate;
        }
        m_rows.push_back(Row{false, {}, msg});
    }
}
