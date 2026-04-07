#ifndef USERDAO_H
#define USERDAO_H

#include <QString>
#include <QVariant>
#include <optional>

struct UserRecord
{
    int id = 0;
    QString username;
    QString passwordHash;
    QString salt;
    QString createdAt;
    QString displayName;
    QString bio;
    QString avatarPath;
};

class UserDao
{
public:
    UserDao() = default;

    bool create(const QString& username, const QString& passwordHash, const QString& salt);
    std::optional<UserRecord> findByUsername(const QString& username);
    bool exists(const QString& username);
    std::optional<QString> getLastRegisterUsername();

    bool updateProfile(int userId, const QString& displayName, const QString& bio, const QString& avatarRelativePath);

    static QString absolutePathFromProjectRelative(const QString& relativePath);
    static QString relativeAvatarPathForUserId(int userId, const QString& suffix);
    static bool ensureAvatarsDirectory();
};

#endif // USERDAO_H
