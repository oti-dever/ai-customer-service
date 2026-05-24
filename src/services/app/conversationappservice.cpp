#include "conversationappservice.h"

#include "../../data/conversationdao.h"
#include "../../data/messagedao.h"
#include "../../data/userdao.h"

QVector<ConversationInfo> ConversationAppService::allConversations() const
{
    ConversationDao dao;
    return dao.listAll();
}

QVector<MessageRecord> ConversationAppService::messages(int conversationId) const
{
    MessageDao dao;
    return dao.listByConversation(conversationId);
}

std::optional<ConversationInfo> ConversationAppService::conversationById(int conversationId) const
{
    ConversationDao dao;
    return dao.findById(conversationId);
}

bool ConversationAppService::isAggregateAutoReplyCandidate(int conversationId) const
{
    ConversationDao cdao;
    const auto conv = cdao.findById(conversationId);
    if (!conv || conv->platform != QLatin1String("qianniu"))
        return false;

    MessageDao mdao;
    const auto last = mdao.lastMessageForConversation(conversationId);
    return last && last->direction == QLatin1String("in");
}

LocalUserProfile ConversationAppService::loadLocalUserProfile(const QString& username) const
{
    LocalUserProfile profile;
    profile.username = username;
    profile.displayName = username;

    UserDao dao;
    const auto user = dao.findByUsername(username);
    if (!user)
        return profile;

    profile.displayName = user->displayName.isEmpty() ? user->username : user->displayName;
    if (!user->avatarPath.isEmpty())
        profile.avatarAbsolutePath = UserDao::absolutePathFromProjectRelative(user->avatarPath);
    return profile;
}
