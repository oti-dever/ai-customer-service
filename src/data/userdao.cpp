#include "userdao.h"
#include "database.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

bool UserDao::create(const QString& username, const QString& passwordHash, const QString& salt)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("INSERT INTO users (username, password_hash, salt) VALUES (:username, :hash, :salt)");
    q.bindValue(":username", username);
    q.bindValue(":hash", passwordHash);
    q.bindValue(":salt", salt);

    if (!q.exec()) {
        qWarning() << "UserDao::创建失败:" << q.lastError().text();
        return false;
    }
    qDebug() << "成功向users表中插入一条用户记录";
    return true;
}

std::optional<UserRecord> UserDao::findByUsername(const QString& username)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT id, username, password_hash, salt, created_at FROM users WHERE username = :username");
    q.bindValue(":username", username);

    if (!q.exec() || !q.next()) {
        qDebug() << "uses表中该用户不存在";
        return std::nullopt;
    }

    UserRecord rec;
    rec.id = q.value("id").toInt();
    rec.username = q.value("username").toString();
    rec.passwordHash = q.value("password_hash").toString();
    rec.salt = q.value("salt").toString();
    rec.createdAt = q.value("created_at").toString();
    return rec;
}

bool UserDao::exists(const QString& username)
{
    return findByUsername(username).has_value();
}

std::optional<QString> UserDao::getLastRegisterUsername()
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("SELECT username FROM users ORDER BY id DESC LIMIT 1");
    if (!q.exec() || !q.next()) {
        qDebug() << "暂无用户注册的最后一条记录";
        return std::nullopt;
    }
    return q.value("username").toString();
}
