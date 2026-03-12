#ifndef AUTHMANAGER_H
#define AUTHMANAGER_H

#include <QObject>
#include <QString>

class AuthManager : public QObject
{
    Q_OBJECT

public:
    explicit AuthManager(QObject* parent = nullptr);

    bool registerUser(const QString& username, const QString& password);

    bool login(const QString& username, const QString& password);

    QString lastError() const { return m_lastError; }
    QString currentUsername() const { return m_currentUsername; }
    bool isLoggedIn() const { return !m_currentUsername.isEmpty(); }

signals:
    void loginSucceeded(const QString& username);
    void loginFailed(const QString& reason);
    void registerSucceeded();
    void registerFailed(const QString& reason);

private:
    void setLastError(const QString& err) { m_lastError = err; }
    bool validateUsername(const QString& username);
    bool validatePassword(const QString& password);

    QString m_lastError;
    QString m_currentUsername;
};

#endif // AUTHMANAGER_H
