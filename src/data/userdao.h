#ifndef USERDAO_H
#define USERDAO_H

#include <QString>
#include <QVariant>
#include <optional>

struct UserRecord
{
    int id;
    QString username;
    QString passwordHash;
    QString salt;
    QString createdAt;
};

class UserDao
{
public:
    UserDao() = default;

    bool create(const QString& username, const QString& passwordHash, const QString& salt);
    std::optional<UserRecord> findByUsername(const QString& username);
    bool exists(const QString& username);
    std::optional<QString> getLastRegisterUsername();
};

#endif // USERDAO_H
