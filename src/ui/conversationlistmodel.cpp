#include "conversationlistmodel.h"

#include <QDateTime>

ConversationListModel::ConversationListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ConversationListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ConversationListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row& row = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return row.conversation.customerName;
    case ConversationIdRole:
        return row.conversation.id;
    case ConversationRole:
        return QVariant::fromValue(row.conversation);
    case LastDirectionRole:
        return row.lastDirection;
    case SelectedRole:
        return row.conversation.id == m_selectedConversationId;
    default:
        return {};
    }
}

void ConversationListModel::setSourceConversations(const QVector<ConversationInfo>& conversations,
                                                   const QHash<int, QString>& lastDirections)
{
    m_allConversations = conversations;
    m_lastDirections = lastDirections;
    rebuildRows();
}

void ConversationListModel::setFilters(int tab,
                                       int platform,
                                       const QString& keyword,
                                       int pendingStickyConversationId)
{
    m_tab = tab;
    m_platform = platform;
    m_keyword = keyword.trimmed();
    m_pendingStickyConversationId = pendingStickyConversationId;
    rebuildRows();
}

void ConversationListModel::setSelectedConversationId(int conversationId)
{
    if (m_selectedConversationId == conversationId)
        return;
    m_selectedConversationId = conversationId;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0), {SelectedRole});
}

int ConversationListModel::selectedConversationId() const
{
    return m_selectedConversationId;
}

int ConversationListModel::conversationIdAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return -1;
    return m_rows.at(row).conversation.id;
}

ConversationInfo ConversationListModel::conversationAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).conversation;
}

QModelIndex ConversationListModel::indexForConversationId(int conversationId) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).conversation.id == conversationId)
            return index(row, 0);
    }
    return {};
}

std::optional<ConversationInfo> ConversationListModel::conversationById(int conversationId) const
{
    for (const Row& row : m_rows) {
        if (row.conversation.id == conversationId)
            return row.conversation;
    }
    return std::nullopt;
}

bool ConversationListModel::containsConversation(int conversationId) const
{
    return conversationById(conversationId).has_value();
}

void ConversationListModel::rebuildRows()
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(m_allConversations.size());
    for (const ConversationInfo& conversation : m_allConversations) {
        const QString lastDirection = m_lastDirections.value(conversation.id);
        if (accepts(conversation, lastDirection))
            m_rows.push_back(Row{conversation, lastDirection});
    }
    endResetModel();
}

bool ConversationListModel::accepts(const ConversationInfo& conversation,
                                    const QString& lastDirection) const
{
    bool inThisTab = false;
    if (m_tab == 0) {
        inThisTab = true;
    } else if (m_tab == 1) {
        inThisTab = lastDirection == QLatin1String("in")
                    || (lastDirection == QLatin1String("out")
                        && m_pendingStickyConversationId == conversation.id);
    } else {
        inThisTab = lastDirection.isEmpty() || lastDirection != QLatin1String("in");
    }
    if (!inThisTab)
        return false;

    const QString platform = platformFilterValue();
    if (!platform.isEmpty() && conversation.platform != platform)
        return false;

    if (!m_keyword.isEmpty()
        && !conversation.customerName.contains(m_keyword, Qt::CaseInsensitive)
        && !conversation.lastMessage.contains(m_keyword, Qt::CaseInsensitive))
        return false;

    return true;
}

QString ConversationListModel::platformFilterValue() const
{
    switch (m_platform) {
    case 1:
        return QStringLiteral("qianniu");
    case 2:
        return QStringLiteral("pdd_web");
    case 3:
        return QStringLiteral("douyin");
    case 4:
        return QStringLiteral("wechat");
    case 0:
    default:
        return {};
    }
}
