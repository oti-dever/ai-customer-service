#include "authmanager.h"
#include "../data/database.h"
#include "../data/userdao.h"
#include "../utils/cryptoutil.h"
#include <QRegularExpression>
#include <QSettings>

AuthManager::AuthManager(QObject* parent)
    : QObject(parent)
{
}

bool AuthManager::registerUser(const QString& username, const QString& password)
{
    m_lastError.clear();

    if (validateUsername(username) == false) {
        emit registerFailed(m_lastError);
        return false;
    }

    if (validatePassword(password) == false) {
        emit registerFailed(m_lastError);
        return false;
    }

    UserDao dao;
    if (dao.exists(username)) {
        setLastError("用户名已存在");
        emit registerFailed(m_lastError);
        return false;
    }

    QString salt = CryptoUtil::generateSalt();
    QString hash = CryptoUtil::hashPassword(password, salt);

    if (!dao.create(username, hash, salt)) {
        setLastError("注册失败，请稍后重试");
        emit registerFailed(m_lastError);
        return false;
    }

    emit registerSucceeded();
    return true;
}

bool AuthManager::login(const QString& username, const QString& password)
{
    m_lastError.clear();

    if (username.trimmed().isEmpty()) {
        setLastError("请输入用户名");
        emit loginFailed(m_lastError);
        return false;
    }
    if (password.isEmpty()) {
        setLastError("请输入密码");
        emit loginFailed(m_lastError);
        return false;
    }

    UserDao dao;
    auto user = dao.findByUsername(username.trimmed());
    if (!user) {
        setLastError("用户名或密码错误");
        emit loginFailed(m_lastError);
        return false;
    }

    if (!CryptoUtil::verifyPassword(password, user->salt, user->passwordHash)) {
        setLastError("用户名或密码错误");
        emit loginFailed(m_lastError);
        return false;
    }

    m_currentUsername = user->username;
    {
        QSettings settings(QStringLiteral("YangYangAI"), QStringLiteral("CustomerServiceDemo"));
        settings.setValue(QStringLiteral("auth/lastLoginUsername"), user->username);
    }
    emit loginSucceeded(m_currentUsername);
    return true;
}

bool AuthManager::validateUsername(const QString& username)
{
    QString u = username.trimmed();
    if (u.isEmpty()) {
        setLastError("用户名不能为空");
        return false;
    }
    if (u.length() < 2) {
        setLastError("用户名至少 2 个字符");
        return false;
    }
    if (u.length() > 18) {
        setLastError("用户名最多 18 个字符");
        return false;
    }
    return true;
}

bool AuthManager::validatePassword(const QString& password)
{
    if (password.length() < 6) {
        setLastError("密码至少 6 位");
        return false;
    }
    if (password.length() > 16) {
        setLastError("密码最多 16 位");
        return false;
    }
    return true;
}
