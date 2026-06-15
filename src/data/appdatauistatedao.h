#ifndef APPDATAUISTATEDAO_H
#define APPDATAUISTATEDAO_H

#include <QString>

class AppDataUiStateDao
{
public:
    AppDataUiStateDao() = default;

    QString draftForConversation(const QString& platform, const QString& conversationKey) const;
    bool saveDraft(const QString& platform, const QString& conversationKey, const QString& content) const;
    bool clearDraft(const QString& platform, const QString& conversationKey) const;
    bool lastSelectedConversation(QString* platform, QString* conversationKey) const;
    bool saveLastSelectedConversation(const QString& platform, const QString& conversationKey) const;
    bool clearLastSelectedConversation() const;

    static QString resolvedAppDataDbPath();
};

#endif // APPDATAUISTATEDAO_H
