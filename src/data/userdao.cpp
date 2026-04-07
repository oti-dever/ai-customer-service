#include "userdao.h"
#include "database.h"
#include <QDir>
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
    q.prepare(
        "SELECT id, username, password_hash, salt, created_at, "
        "IFNULL(display_name, '') AS display_name, IFNULL(bio, '') AS bio, "
        "IFNULL(avatar_path, '') AS avatar_path "
        "FROM users WHERE username = :username");
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
    rec.displayName = q.value(QStringLiteral("display_name")).toString();
    rec.bio = q.value(QStringLiteral("bio")).toString();
    rec.avatarPath = q.value(QStringLiteral("avatar_path")).toString();
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

bool UserDao::updateProfile(int userId, const QString& displayName, const QString& bio, const QString& avatarRelativePath)
{
    QSqlQuery q(Database::getInstance().connection());
    q.prepare("UPDATE users SET display_name = :d, bio = :b, avatar_path = :a WHERE id = :id");
    q.bindValue(":d", displayName);
    q.bindValue(":b", bio);
    q.bindValue(":a", avatarRelativePath);
    q.bindValue(":id", userId);
    if (!q.exec()) {
        qWarning() << "UserDao::updateProfile failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QString UserDao::absolutePathFromProjectRelative(const QString& relativePath)
{
    return QDir(QStringLiteral(PROJECT_ROOT_DIR)).absoluteFilePath(relativePath);
}

QString UserDao::relativeAvatarPathForUserId(int userId, const QString& suffix)
{
    return QStringLiteral("database/avatars/%1%2").arg(userId).arg(suffix);
}

bool UserDao::ensureAvatarsDirectory()
{
    QDir root(QStringLiteral(PROJECT_ROOT_DIR));
    return root.mkpath(QStringLiteral("database/avatars"));
}
