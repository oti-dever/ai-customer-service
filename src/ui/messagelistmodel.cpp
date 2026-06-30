#include "messagelistmodel.h"

#include <QDateTime>
#include <QSet>
#include <QStringList>

namespace {

constexpr qint64 kMessageTimeSeparatorIntervalSecs = 10 * 60;

QDateTime displayMessageTime(const MessageRecord& message)
{
    return message.createdAt.isValid() ? message.createdAt : QDateTime::currentDateTime();
}

QString messageTimeSeparatorText(const QDateTime& time, const QDateTime& previousTime)
{
    const QDate date = time.date();
    if (!previousTime.isValid() || previousTime.date() != date) {
        const QDate today = QDate::currentDate();
        QString dayText;
        if (date == today)
            dayText = QStringLiteral("今天");
        else if (date == today.addDays(-1))
            dayText = QStringLiteral("昨天");
        else
            dayText = date.toString(QStringLiteral("MM/dd"));
        return QStringLiteral("%1 %2").arg(dayText, time.toString(QStringLiteral("HH:mm")));
    }
    return time.toString(QStringLiteral("HH:mm"));
}

bool shouldInsertTimeSeparator(const QDateTime& time, const QDateTime& previousTime)
{
    if (!previousTime.isValid())
        return true;
    if (previousTime.date() != time.date())
        return true;
    return previousTime.secsTo(time) >= kMessageTimeSeparatorIntervalSecs;
}

} // namespace

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
        case SeparatorTextRole:
            return row.separatorText;
        case Qt::DisplayRole:
            return row.separatorText;
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
    QDateTime lastMsgTime;
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (m_rows[i].separator)
            continue;
        lastMsgTime = displayMessageTime(m_rows[i].message);
        break;
    }
    const QDateTime msgTime = displayMessageTime(message);
    const QDate msgDate = msgTime.date();
    const bool needsSeparator = shouldInsertTimeSeparator(msgTime, lastMsgTime);
    const int first = m_rows.size();
    const int last = first + (needsSeparator ? 1 : 0);
    beginInsertRows(QModelIndex(), first, last);
    m_messages.push_back(message);
    if (needsSeparator) {
        m_rows.push_back(Row{true, msgDate, messageTimeSeparatorText(msgTime, lastMsgTime), {}});
    }
    m_rows.push_back(Row{false, {}, {}, message});
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
    QDateTime lastMsgTime;
    for (const MessageRecord& msg : m_messages) {
        const QDateTime msgTime = displayMessageTime(msg);
        const QDate msgDate = msgTime.date();
        if (shouldInsertTimeSeparator(msgTime, lastMsgTime)) {
            m_rows.push_back(Row{true, msgDate, messageTimeSeparatorText(msgTime, lastMsgTime), {}});
        }
        m_rows.push_back(Row{false, {}, {}, msg});
        lastMsgTime = msgTime;
    }
}
