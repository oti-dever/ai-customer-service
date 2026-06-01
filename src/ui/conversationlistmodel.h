#ifndef CONVERSATIONLISTMODEL_H
#define CONVERSATIONLISTMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QVector>
#include <optional>
#include "../core/types.h"

class ConversationListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        ConversationIdRole = Qt::UserRole + 1,
        ConversationRole,
        LastDirectionRole,
        SelectedRole,
    };

    explicit ConversationListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void setSourceConversations(const QVector<ConversationInfo>& conversations,
                                const QHash<int, QString>& lastDirections);
    void setFilters(int tab,
                    int platform,
                    const QString& keyword,
                    int pendingStickyConversationId);
    void setSelectedConversationId(int conversationId);

    int selectedConversationId() const;
    int conversationIdAt(int row) const;
    ConversationInfo conversationAt(int row) const;
    QModelIndex indexForConversationId(int conversationId) const;
    std::optional<ConversationInfo> conversationById(int conversationId) const;
    bool containsConversation(int conversationId) const;

private:
    struct Row {
        ConversationInfo conversation;
        QString lastDirection;
    };

    void rebuildRows();
    bool accepts(const ConversationInfo& conversation, const QString& lastDirection) const;
    QString platformFilterValue() const;

    QVector<ConversationInfo> m_allConversations;
    QHash<int, QString> m_lastDirections;
    QVector<Row> m_rows;
    int m_selectedConversationId = -1;
    int m_tab = 1;
    int m_platform = 0;
    QString m_keyword;
    int m_pendingStickyConversationId = -1;
};

#endif // CONVERSATIONLISTMODEL_H
