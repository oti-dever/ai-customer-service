#include <QtTest>

#include "core/authmanager.h"
#include "data/userdao.h"
#include "testdatabase.h"

class TestAuthManager : public QObject
{
    Q_OBJECT

private slots:
    void registerUser_persistsHashedCredentials();
    void registerUser_rejectsDuplicateUsername();
    void login_setsCurrentUserAndRejectsWrongPassword();
};

void TestAuthManager::registerUser_persistsHashedCredentials()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    AuthManager auth;
    QSignalSpy okSpy(&auth, &AuthManager::registerSucceeded);
    QSignalSpy failSpy(&auth, &AuthManager::registerFailed);

    QVERIFY(auth.registerUser(QStringLiteral("alice"), QStringLiteral("secret1")));
    QCOMPARE(okSpy.count(), 1);
    QCOMPARE(failSpy.count(), 0);

    UserDao dao;
    const auto user = dao.findByUsername(QStringLiteral("alice"));
    QVERIFY(user.has_value());
    QCOMPARE(user->username, QStringLiteral("alice"));
    QVERIFY(!user->salt.isEmpty());
    QVERIFY(!user->passwordHash.isEmpty());
    QVERIFY(user->passwordHash != QStringLiteral("secret1"));
}

void TestAuthManager::registerUser_rejectsDuplicateUsername()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    AuthManager auth;
    QVERIFY(auth.registerUser(QStringLiteral("alice"), QStringLiteral("secret1")));

    QSignalSpy failSpy(&auth, &AuthManager::registerFailed);
    QVERIFY(!auth.registerUser(QStringLiteral("alice"), QStringLiteral("secret2")));
    QCOMPARE(failSpy.count(), 1);
    QCOMPARE(auth.lastError(), QStringLiteral("用户名已存在"));
}

void TestAuthManager::login_setsCurrentUserAndRejectsWrongPassword()
{
    ScopedTestDatabase db;
    Q_UNUSED(db);

    AuthManager auth;
    QVERIFY(auth.registerUser(QStringLiteral("alice"), QStringLiteral("secret1")));

    QSignalSpy loginOkSpy(&auth, &AuthManager::loginSucceeded);
    QVERIFY(auth.login(QStringLiteral("alice"), QStringLiteral("secret1")));
    QCOMPARE(loginOkSpy.count(), 1);
    QVERIFY(auth.isLoggedIn());
    QCOMPARE(auth.currentUsername(), QStringLiteral("alice"));

    AuthManager wrongAuth;
    QSignalSpy loginFailSpy(&wrongAuth, &AuthManager::loginFailed);
    QVERIFY(!wrongAuth.login(QStringLiteral("alice"), QStringLiteral("bad-pass")));
    QCOMPARE(loginFailSpy.count(), 1);
    QCOMPARE(wrongAuth.lastError(), QStringLiteral("用户名或密码错误"));
}

QTEST_MAIN(TestAuthManager)
#include "test_authmanager.moc"
